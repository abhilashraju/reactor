#pragma once
#include "asyncserver.hpp"
#include "common/common_defs.hpp"
#include "common/utilities.hpp"
#include "http_errors.hpp"
#include "http_handler.hpp"
#include "http_target_parser.hpp"
#include "request_mapper.hpp"
#include "server_concepts.hpp"
#include "utils/flat_map.hpp"
namespace reactor
{

struct HttpRouter
{
    struct handler_base
    {
        virtual VariantResponse handle(VariantRequest& req,
                                       const http_function& vw) = 0;
        virtual ~handler_base() {}
    };
    using HANDLER_MAP = flat_map<request_mapper, std::unique_ptr<handler_base>>;
    template <typename HandlerFunc>
    struct handler : handler_base
    {
        HandlerFunc func;
        handler(HandlerFunc fun) : func(std::move(fun)) {}

        VariantResponse handle(VariantRequest& req,
                               const http_function& params) override
        {
            return func(req, params);
        }
    };
    void setIoContext(std::reference_wrapper<net::io_context> ctx)
    {
        ioc = ctx;
    }
    template <typename FUNC>
    void add_handler(const request_mapper& mapper, FUNC&& h)
    {
        auto& handlers = handler_for_verb(mapper.method);
        handlers[mapper] = std::make_unique<handler<FUNC>>(std::move(h));
    }
    template <typename FUNC>
    void add_get_handler(std::string_view path, FUNC&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::get}, (FUNC&&)h);
    }
    template <DynBodyRequestHandler Handler>
    void add_get_handler(std::string_view path, Handler&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::get},
                    DynamicBodyHandler()((Handler&&)h));
    }
    template <StringBodyRequestHandler Handler>
    void add_get_handler(std::string_view path, Handler&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::get},
                    StringBodyHandler()((Handler&&)h));
    }

    template <typename FUNC>
    void add_post_handler(std::string_view path, FUNC&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::post},
                    (FUNC&&)h);
    }
    template <DynBodyRequestHandler Handler>
    void add_post_handler(std::string_view path, Handler&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::post},
                    DynamicBodyHandler()((Handler&&)h));
    }
    template <StringBodyRequestHandler Handler>
    void add_post_handler(std::string_view path, Handler&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::post},
                    StringBodyHandler()((Handler&&)h));
    }

    template <typename FUNC>
    void add_put_handler(std::string_view path, FUNC&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::put}, (FUNC&&)h);
    }
    template <DynBodyRequestHandler Handler>
    void add_put_handler(std::string_view path, Handler&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::put},
                    DynamicBodyHandler()((Handler&&)h));
    }
    template <typename FUNC>
    void add_delete_handler(std::string_view path, FUNC&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::delete_}, path,
                    (FUNC&&)h);
    }

    HANDLER_MAP& handler_for_verb(http::verb v)
    {
        switch (v)
        {
            case http::verb::get:
                return get_handlers;
            case http::verb::put:
            case http::verb::post:
                return post_handlers;
            case http::verb::delete_:
                return delete_handlers;
        }
        return get_handlers;
    }

    auto process_request(auto& reqVariant)
    {
        auto httpfunc = parse_function(target(reqVariant));
        auto& handlers = handler_for_verb(method(reqVariant));
        if (auto iter = handlers.find({httpfunc.name(), method(reqVariant)});
            iter != std::end(handlers))
        {
            extract_params_from_path(httpfunc, iter->first.path,
                                     httpfunc.name());
            return iter->second->handle(reqVariant, httpfunc);
        }

        throw file_not_found(httpfunc.name());
    }
    void operator()(VariantRequest&& req, auto&& cont)
    {
        net::spawn(ioc->get().get_executor(),
                   [this, req = std::move(req),
                    cont = std::move(cont)](net::yield_context yield) mutable {
            try
            {
                auto response = process_request(req);
                cont(std::move(response));
            }
            catch (std::exception& e)
            {
                REACTOR_LOG_ERROR("Error processing request:{} ", e.what());

                cont(make_error_response(req, e));
            }
        });
    }
    VariantResponse make_error_response(VariantRequest& req, std::exception& e)
    {
        http::status status = http::status::not_found;
        std::string_view message = target(req);
        http::response<http::string_body> res{http::status::not_found, 11};
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(false);
        res.body() = std::format("Not Found {}", message);
        res.prepare_payload();
        return res;
    }

    HttpRouter* getForwarder(const std::string& path)
    {
        return this;
    }

    HANDLER_MAP get_handlers;
    HANDLER_MAP post_handlers;
    HANDLER_MAP delete_handlers;
    std::optional<std::reference_wrapper<net::io_context>> ioc;
};

struct HttpsServer
{
    using HttServerHandler = HttpHandler<HttpRouter>;
    HttpRouter router_;
    HttServerHandler handler{router_};
    AsyncSslServer<HttServerHandler> server;
    HttpsServer(std::string_view port, std::string_view cert) :
        server(handler, port, cert)
    {}
    void start()
    {
        server.start();
    }
    HttpRouter& router()
    {
        return router_;
    }
};
} // namespace reactor
