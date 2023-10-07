#pragma once
#include "core/reactor_concepts.hpp"

#include <functional>
namespace reactor
{

struct SubscriberBase : std::enable_shared_from_this<SubscriberBase>
{
    virtual ~SubscriberBase() {}
};
template <typename T, typename Type>
struct SubscriberType : SubscriberBase
{
    using Base = SubscriberBase;
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
    void visit()
    {
        std::visit(
            [this](auto& handler) { invokeSubscriberWithNoValue(handler); },
            subscriber);
    }
    void invokeSubscriberWithNoValue(auto& handler)
    {
        self().subscribe(std::move(handler));
    }
};

template <typename SrcType, typename DestType, typename ParentAdapter,
          bool Filterer = false>
struct Adapter :
    SubscriberType<DestType,
                   Adapter<SrcType, DestType, ParentAdapter, Filterer>>
{
    struct LazyAdaptee
    {
        Adapter adapter;
        std::shared_ptr<SubscriberBase> ownerAdaptee;
        void subscribe(auto handler)
        {
            adapter.subscribe(std::move(handler));
        }
    };
    using AdaptFuncion = std::function<DestType(const SrcType&)>;
    using FilterFunction = std::function<bool(const SrcType&)>;

    using Base =
        SubscriberType<DestType,
                       Adapter<SrcType, DestType, ParentAdapter, Filterer>>;
    AdaptFuncion adaptFunc;
    FilterFunction filterFunc;
    ParentAdapter* src{nullptr};
    Adapter(AdaptFuncion func, ParentAdapter* s,
            FilterFunction filt = FilterFunction{}) :
        adaptFunc(std::move(func)),
        filterFunc(std::move(filt)), src(s)
    {}

    void operator()(const SrcType& res, auto&& reqNext)
    {
        if constexpr (Filterer)
        {
            if (filterFunc(res))
            {
                Base::visit(res);
            }
            else
            {
                Base::visit();
            }
        }
        else
        {
            Base::visit(adaptFunc(res));
        }
    }
    void subscribe(auto handler)
    {
        Base::subscriber = std::move(handler);
        src->subscribe(*this);
    }

    auto map(MapFunction<DestType> auto mapFun)
    {
        using FuncType = decltype(mapFun);
        using NewDestType = std::invoke_result_t<FuncType, DestType>;
        auto adapter = new Adapter<DestType, NewDestType, Adapter>(
            std::move(mapFun), this);
        adapter->rootAdaptee()->addToMappers(adapter);
        return *adapter;
    }
    auto filter(std::function<bool(const DestType&)> filtFun)
    {
        auto identityfunc = [](const DestType& v) { return v; };
        auto adapter = new Adapter<DestType, DestType, Adapter, true>(
            std::move(identityfunc), this, std::move(filtFun));
        adapter->rootAdaptee()->addToMappers(adapter);
        return *adapter;
    }
    auto rootAdaptee()
    {
        return src->rootAdaptee();
    }
    auto makeLazy()
    {
        return LazyAdaptee{*this, rootAdaptee()->getSharedPtr()};
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
    std::vector<std::unique_ptr<SubscriberBase>> mapHandlers;

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

    auto map(MapFunction<T> auto mapFun)
    {
        using FuncType = decltype(mapFun);
        using DestType = std::invoke_result_t<FuncType, T>;
        auto adapter = new Adapter<T, DestType, FluxBase>(std::move(mapFun),
                                                          this);
        addToMappers(adapter);
        return *adapter;
    }
    auto filter(std::function<bool(const T&)> filtFun)
    {
        auto identityfunc = [](const T& v) { return v; };
        auto adapter = new Adapter<T, T, FluxBase, true>(
            std::move(identityfunc), this, std::move(filtFun));
        addToMappers(adapter);
        return *adapter;
    }

    auto rootAdaptee()
    {
        return this;
    }
    void addToMappers(SubscriberBase* mapper)
    {
        mapHandlers.push_back(std::unique_ptr<SubscriberBase>(mapper));
    }
    auto getSharedPtr()
    {
        return Base::shared_from_this();
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
    Mono() : Base(nullptr) {}
    static Mono just(T v)
    {
        return Mono{new Just(std::move(v))};
    }
    static std::shared_ptr<Mono> justPtr(T v)
    {
        return std::make_shared<Mono>(new Just(std::move(v)));
    }
    struct Generator : Base::SourceHandler
    {
        using GeneratorFunc = std::function<T()>;
        GeneratorFunc generator;
        bool mHasNext{true};
        explicit Generator(GeneratorFunc f) : generator(std::move(f)) {}
        void next(std::function<void(T)> consumer) override
        {
            mHasNext = false;
            consumer(generator());
        }
        bool hasNext() const override
        {
            return mHasNext;
        }
    };
    static Mono justFrom(Generator::GeneratorFunc f)
    {
        return Mono{new Generator(std::move(f))};
    }
    static std::shared_ptr<Mono> justPtr(Generator::GeneratorFunc f)
    {
        return std::make_shared<Mono>(new Generator(std::move(f)));
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
    Flux() : Base(nullptr) {}
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
struct AsyncSinkGroup
{
    using TargetSinkType = std::variant<SinkTypes...>;

    using Sinks = std::vector<TargetSinkType>;
    Sinks targetSinks;
    Sinks tobeCleared;
    std::function<void(bool)> requestNext;
    bool nextNeeded{false};
    int sinkExecutionCount{0};
    AsyncSinkGroup(Sinks sinks) : targetSinks(std::move(sinks)) {}
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
            sink(res,
                 std::bind_front(&AsyncSinkGroup::handleSinkCallback, this));
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

template <typename SourceType, typename... SinkTypes>
struct SyncSinkGroup
{
    using TargetSinkType = std::variant<SinkTypes...>;

    using Sinks = std::vector<TargetSinkType>;
    Sinks targetSinks;

    SyncSinkGroup(Sinks sinks) : targetSinks(std::move(sinks)) {}

    void process(const SourceType& res, auto& vsink)
    {
        std::visit([&res](auto& sink) { sink(res); }, vsink);
    }
    void operator()(const SourceType& res)
    {
        for (auto& vsink : targetSinks)
        {
            process(res, vsink);
        }
    }
};
template <typename T, typename... Sink>
inline auto createSinkGroup(Sink... sink)
{
    using BroadCaster = SyncSinkGroup<T, Sink...>;
    using BroadCasterSinks = typename BroadCaster::Sinks;
    return BroadCaster{BroadCasterSinks{std::move(sink)...}};
}
} // namespace reactor
