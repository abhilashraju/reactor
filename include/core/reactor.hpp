#pragma once
namespace reactor
{
template <typename T, typename Type>
struct SubscriberType
{
    using value_type = T;
    using CompletionToken = std::function<void(bool)>;
    using AsyncSubscriber =
        std::function<void(const value_type&, CompletionToken&&)>;
    using SyncSubscriber = std::function<void(const value_type&)>;
    using Subscriber = std::variant<SyncSubscriber, AsyncSubscriber>;
    Subscriber subscriber;
    Type& self()
    {
        return *static_cast<Type*>(this);
    }
    void invokeSubscriber(const value_type& r, AsyncSubscriber& handler)
    {
        handler(r, [this](bool next) {
            if (next)
            {
                self().subscribe(
                    std::move(std::get<AsyncSubscriber>(subscriber)));
            }
        });
    }
    void invokeSubscriber(const value_type& r, SyncSubscriber& handler)
    {
        handler(r);
        self().subscribe(std::move(handler));
    }
    void visit(const value_type& r)
    {
        std::visit([&r, this](auto& handler) { invokeSubscriber(r, handler); },
                   subscriber);
    }
};

template <typename SrcType, typename DestType, typename Source>
struct Adapter : SubscriberType<DestType, Adapter<SrcType, DestType, Source>>
{
    using AdaptFuncion = std::function<DestType(const SrcType&)>;

    using Base = SubscriberType<DestType, Adapter<SrcType, DestType, Source>>;
    AdaptFuncion adaptFunc;
    Source* src{nullptr};
    Adapter(AdaptFuncion func, Source* s) : adaptFunc(std::move(func)), src(s)
    {}
    void operator()(const SrcType& res, auto&& reqNext)
    {
        Base::visit(adaptFunc(res));
    }
    void subscribe(auto handler)
    {
        Base::subscriber = std::move(handler);
        src->subscribe(*this);
    }
    template <typename NewDestType>
    auto map(std::function<NewDestType(const DestType&)> mapFun)
    {
        return Adapter<DestType, NewDestType, Adapter>(std::move(mapFun), this);
    }
};

template <typename T>
struct FluxBase : SubscriberType<T, FluxBase<T>>
{
    using value_type = T;
    using Base = SubscriberType<T, FluxBase<T>>;

    struct SourceHandler
    {
        virtual void next(std::function<void(T)> consumer) = 0;
        virtual bool hasNext() const = 0;
        virtual ~SourceHandler() {}
    };

  protected:
    explicit FluxBase(SourceHandler* srcHandler) : mSource(srcHandler) {}
    std::unique_ptr<SourceHandler> mSource{};
    std::function<void()> onFinishHandler{};

  public:
    void subscribe(auto handler)
    {
        Base::subscriber = std::move(handler);
        if (mSource->hasNext())
        {
            mSource->next([handler = std::move(handler), this](const T& v) {
                Base::visit(v);
            });
            return;
        }
        if (onFinishHandler)
        {
            onFinishHandler();
        }
    }
    FluxBase& onFinish(std::function<void()> finish)
    {
        onFinishHandler = std::move(finish);
        return *this;
    }
    template <typename DestType>
    auto map(std::function<DestType(const T&)> mapFun)
    {
        return Adapter<T, DestType, FluxBase>(std::move(mapFun), this);
    }
};
template <typename T>
struct Mono : FluxBase<T>
{
    using Base = FluxBase<T>;
    struct Just : Base::SourceHandler
    {
        T value{};
        bool mHasNext{true};
        explicit Just(T v) : value(std::move(v)) {}
        void next(std::function<void(T)> consumer) override
        {
            mHasNext = false;
            consumer(std::move(value));
        }
        bool hasNext() const override
        {
            return mHasNext;
        }
    };

    explicit Mono(Base::SourceHandler* srcHandler) : Base(srcHandler) {}

    static Mono just(T v)
    {
        return Mono{new Just(std::move(v))};
    }
};

template <typename T>
struct Flux : FluxBase<T>
{
    using Base = FluxBase<T>;
    template <class R>
    struct Range : Base::SourceHandler
    {
        R range{};
        R::iterator current{};
        explicit Range(R v) : range(std::move(v)), current(range.begin()) {}
        void next(std::function<void(T)> consumer) override
        {
            T v = std::move(*current);
            ++current;
            consumer(std::move(v));
        }
        bool hasNext() const override
        {
            return current != range.end();
        }
    };

    explicit Flux(Base::SourceHandler* srcHandler) : Base(srcHandler) {}
    template <class R>
    static Flux range(R v)
    {
        return Flux{new Range<R>(std::move(v))};
    }

    struct Generator : Base::SourceHandler
    {
        using GeneratorFunc = std::function<T(bool& next)>;
        GeneratorFunc generator;
        bool nextFound{true};
        explicit Generator(GeneratorFunc f) : generator(std::move(f)) {}
        void next(std::function<void(T)> consumer) override
        {
            consumer(generator(nextFound));
        }
        bool hasNext() const override
        {
            return nextFound;
        }
    };

    static Flux generate(Generator::GeneratorFunc f)
    {
        return Flux{new Generator(std::move(f))};
    }
};
template <typename SourceType, typename... SinkTypes>
struct SinkGroup
{
    using TargetSinkType = std::variant<SinkTypes...>;

    using Sinks = std::vector<TargetSinkType>;
    Sinks targetSinks;
    Sinks tobeCleared;
    std::function<void(bool)> requestNext;
    bool nextNeeded{false};
    int sinkExecutionCount{0};
    SinkGroup(Sinks sinks) : targetSinks(std::move(sinks)) {}
    void handleSinkCallback(bool next)
    {
        sinkExecutionCount++;
        nextNeeded |= next;
        if (sinkExecutionCount == targetSinks.size())
        {
            requestNext(nextNeeded);
        }
    }
    void process(const SourceType& res, auto& vsink)
    {
        std::visit(
            [&res, this](auto& sink) {
            sink(res, std::bind_front(&SinkGroup::handleSinkCallback, this));
            },
            vsink);
    }
    void operator()(const SourceType& res, auto&& reqNext)
    {
        requestNext = std::move(reqNext);
        sinkExecutionCount = 0;
        nextNeeded = false;
        for (auto& sink : targetSinks)
        {
            process(res, sink);
        }
    }
};
} // namespace reactor
