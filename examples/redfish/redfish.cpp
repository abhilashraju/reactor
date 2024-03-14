#include "command_line_parser.hpp"
#include "http/web_client.hpp"
using namespace reactor;
struct Requester
{
    std::string token;
    std::string machine;
    ssl::context ctx{ssl::context::tlsv12_client};
    net::io_context& ioc;
    std::string usename;
    std::string password_;
    Requester(net::io_context& ioc) : ioc(ioc) {}
    Requester& withCredentials(std::string_view username, std::string_view pass)
    {
        usename = std::string(username.data(), username.size());
        password_ = std::string(pass.data(), pass.size());
        token = std::string();
        return *this;
    }
    Requester& withMachine(std::string machine)
    {
        this->machine = machine;
        return *this;
    }

    std::string user()
    {
        return usename;
    }
    std::string password()
    {
        return password_;
    }
    ssl::context& getContext()
    {
        ctx.set_verify_mode(ssl::verify_none);
        return ctx;
    }
    void getToken()
    {
        getToken([]() {});
    }
    template <typename Contiuation>
    void getToken(Contiuation cont)
    {
        if (token.empty())
        {
            auto mono =
                WebClient<AsyncSslStream, http::string_body>::builder()
                    .withSession(ioc.get_executor(), getContext())
                    .withEndpoint(std::format(
                        "https://{}.aus.stglabs.ibm.com:443/redfish/v1/SessionService/Sessions",
                        machine))
                    .create()
                    .post()
                    .withBody(nlohmann::json{{"UserName", user()},
                                             {"Password", password()}})
                    .toMono();
            mono->asJson([this, mono, cont = std::move(cont)](auto& v) {
                if (v.isError())
                {
                    REACTOR_LOG_ERROR("Error: {}", v.error().message());
                    return;
                }
                REACTOR_LOG_ERROR("Error: {}", v.response().data().dump(4));
                token = v.response().getHeaders()["X-Auth-Token"];
                cont();
            });
            return;
        }
        cont();
    }
    template <typename Contiuation>
    void get(const std::string& target, Contiuation cont)
    {
        getToken([this, cont = std::move(cont), target = target]() {
            std::string ep = std::format(
                "https://{}.aus.stglabs.ibm.com:443/{}", machine, target);
            auto mono = WebClient<AsyncSslStream, http::string_body>::builder()
                            .withSession(ioc.get_executor(), getContext())
                            .withEndpoint(ep)
                            .create()
                            .get()
                            .withHeader({"X-Auth-Token", token})
                            .toMono();
            mono->asJson([cont = std::move(cont), mono](auto v) {
                if (v.isError())
                {
                    REACTOR_LOG_ERROR("Error: {}", v.error().message());
                    return;
                }
                cont(v.response().data());
            });
        });
    }
    template <typename... Contiuation>
    void when_all(Contiuation... cont)
    {
        auto f = std::make_tuple(cont...);
        std::apply([this](auto&&... cont) { (cont(), ...); }, f);
    }
};
template <typename... Contiuation>
struct When_All
{
    using tuple_type = std::tuple<Contiuation...>;
    static constexpr size_t size = std::tuple_size_v<tuple_type>;
    tuple_type cont_;
    using ResultType =
        std::array<nlohmann::json, std::tuple_size_v<tuple_type>>;
    using FinishHandler = std::function<void(ResultType&)>;
    ResultType results;
    FinishHandler on_finish_;
    size_t counter = size;
    struct WhenAllRequester
    {
        When_All* all;
        Requester* requester;
        size_t index;
        void get(const std::string& target, auto&& cont)
        {
            auto incrementer = [all = all,
                                cont = std::move(cont)](const auto& v) {
                all->results[all->size - all->counter] = cont(v);
                all->finish();
            };
            requester->get(target, incrementer);
        }
    };
    When_All(Contiuation... cont) : cont_(std::make_tuple(cont...)) {}

    void onFinish(FinishHandler on_finish)
    {
        on_finish_ = std::move(on_finish);
    }
    void finish()
    {
        if (--counter == 0)
        {
            on_finish_(results);
        }
    }
    void operator()(Requester& requester)
    {
        std::apply(
            [this, &requester](auto&&... cont) {
            (
                [&]() {
                WhenAllRequester req{.all = this, .requester = &requester};
                cont(req);
            }(),
                ...);
        },
            cont_);
    }
};
int main(int argc, const char* argv[])
{
    auto [user, password] = getArgs(parseCommandline(argc, argv), "-u", "-p");
    if (user.empty() || password.empty())
    {
        std::cout << "redfish -u <username> -p <password>\n";
        return 1;
    }
    net::io_context ioc;
    Requester requester(ioc);

    requester.withCredentials(user, password)
        .withMachine("rain104bmc")
        .getToken();
    auto chassis = [](auto& requester) {
        requester.get("redfish/v1/Chassis/System", [](auto& v) { return v; });
    };
    auto cables = [](auto& requester) {
        requester.get("redfish/v1/Cables", [](auto& v) { return v; });
    };
    When_All all(chassis, cables);
    all.onFinish([](auto& results) {
        REACTOR_LOG_DEBUG("Done");
        for (auto& v : results)
        {
            REACTOR_LOG_DEBUG("{}", v.dump(4));
        }
    });

    all(requester);
    ioc.run();
}
