
# Reactor

Author: Abhilash Raju (Abhir) 

Other contributors: None

Created: Oct 8, 2023

## Problem Description

Reactor is and implementation of reactive programming paradigm. The project is inspired from [its spring boot implementation](https://projectreactor.io/docs/core/release/reference/#getting-started-introducing-reactor).This is to simplify the asynchronouse programming patterns.BmcWeb can be a potential client for the library. Today we use callback tokens prescribed by boost asio to solve asynchronous IO. But these callbacks are very hard to compose together, quickly leading to code that is difficult to read and maintain. Hopefully, the Reactor APIs can help in writing useful higher level abstractions that can be composed easily to solve asynchronous IO problems.

## Background and References

https://projectreactor.io/docs/core/release/reference/#getting-started-introducing-reactor


## Requirements

We need better APIs that can give useful abstraction for application developer to deal with asynchronous concurrent tasks. The APIs should promote better readable and maintenable code by providing useful abstractions to eleminate boilerplate code. The reactive programming is modern day solution to absorbe complexities involved in developement of concurrent programs. It tries to hide explicit synchronisation needs between tasks from developer, thus helping him/her focus on the domain specific computations. The declarative style that focus on "what" rather that "how" part of the computation will helps in produce more readable and maintainable code. The following are some of the important feature that can help in creating  maintainable application. 

#### Composability and Readability

An asynchronouse computation can be represented as a declarative task graphs consists of several composable subtasks. The resulting computation graphs can be submitted to a scheduler. Separating a task from its execution context reduces the complexity ,improves readability and reduces the maintainance cost. Moreover,the developement of domain specific logic and the enahancements to scheduler algorithms can prgress independently because of the loose couplings between them.  

#### The Assembly Line Analogy

The computation graph can be structure as an assembly line. The data will be originated from a Source(Publisher) and lands at a Sink(Subscriber). Along the path the data will be subjected to several transformations and filterings.

#### Operators(Maps and Filters)

Operator are the tools that can transform the data while it passes through the assembly lines. These opeartors are easily composable.You just need to make sure that operators have compatible output/input data types. Separation of computation in to smaller operator will make it easy to write readable and maintainable code. 

#### Lazy Evaluation

The computation graph creation itself does not do any processing. The resulting graph is a first class object that can be  moved to another execution context or saved for later execution. The execution will be started when you attach a Sink(Subscriber) to it. 

#### Backpressure

A Sink can controll the Source's data production. Sink can do it by calling request next after processing the current data.This way a Sink can backpressure the Source. 

## Proposed Design

The reactor APIs should promote code that describes data flow in a declarative manner. So the APIs should be set of composable abstractions that cane be fitted together to solve application's domain specific problems. This is analogous to how plumbing work has been done using pipes and connectors to redirect water flow. In our case the water is the Data and connectors are the Source, Operators  and the Sink. The  skeleton of a  reactive application will look like below. 

```ascii

┌────────┐      xxxx         xxxx     ┌────────┐
│        │     x    x       x    x    │        │
│ Source ├────►x Op x──────►x Op x───►│  Sink  │
│        │     x    x       x    x    │        │
└────────┘      xxxx         xxxx     └────────┘

```
Data will be produced by a Source(Publisher) and lands in Sink (Subscriber). The data might be transformed by several operators on its way to Sink. 
The framework comes with several readymade Publisher, Subscriber and set of opearators. 
Applcation can creates its own publisher and subscriber. 

#### Publisher
Publisher is the producer of data.A publisher can produce values both synchronousely and asynchronousely. In both cases pipeline code looks similar. A publisher should have datastructures in place to handle back pressure from Subscriber. 

Publishers are of two types Mono and Flux.
A Mono will produce at max one value and finishes after it
A Flux can produce finite or infinite set of values.
 
#### Subscriber

Subscriber are the final consumer of the data. Subscriber could be a database or just an action handler that sends data to outside world such as a Rest Client. A subscriber can be a publisher for new data flow chain. This way we can create complex data flow chains from simpler data flow parts. Through creative implementation of Sink we can create Fork and Join abstractions.

#### Operators

Each operator adds behavior to a Publisher and wraps the previous step’s Publisher into a new instance. The whole chain is thus linked, such that data originates from the first Publisher and moves down the chain, transformed by each link. Eventually, a Subscriber finishes the process
Map and filter are two major operators,with which we can do most of the data transormation. While map operation transforms the data in transit, the filter operation  suppress the propogation of data according to filter rules. The transformation done at map can potentially change the data type fo data. A filter will never chnage the data type as it is intented to just filter the data.

### Error Handling 

All errors and exceptions occured duing the data sourcing and and operator processing phases are captured and propogated via error channel to the subcriber. Error channel is separate function ,should be implemented by the Subscriber to catch errors occured in the chain. The error occured inside the  subcriber handler will not be captured the error reporting mechanism. It is is upto the subscriber to deal with graceful handling of such errors.

### Examples

Integer Mono
```
    bool finished = false;
    auto m = Mono<int>::just(10);
    m.onFinish([&finished]() { finished = true; }).subscribe([](auto v) {
        EXPECT_EQ(v, 10);
    });
    EXPECT_EQ(finished, true);

```
The above code demonstrates the use of an Mono that produces one integer value and finishes.

String Flux
```
    bool finished{false};
    std::vector<std::string> captured;
    auto ins = std::back_inserter(captured);
    auto m2 = Flux<std::string>::range(std::vector<std::string>{"hi", "hello"});
    m2.onFinish([&finished]() { finished = true; }).subscribe([&ins](auto v) {
        *ins = v;
    });
    std::vector totest = {"hi", "hello"};
    EXPECT_EQ(std::equal(begin(captured), end(captured), begin(totest)), true);
    EXPECT_EQ(finished, true);
```
The above code is an example of flux in which list strings were originated from source and lands in sink. Then from the sink function we are capturing the data in another container for varification. 

Custom Generator
```
    auto m2 = Flux<std::string>::generate(
        [myvec = std::vector<std::string>{"hi", "hello"},
         i = 0](bool& hasNext) mutable {
        auto ret = myvec.at(i++);
        hasNext = i < myvec.size();
        return ret;
    });
    std::vector<std::string> captured;
    auto ins = std::back_inserter(captured);
    m2.subscribe([&ins](auto v, auto reqNext) {
        *ins = v;
        reqNext(true);
    });
    std::vector expected = {"hi", "hello"};
    EXPECT_EQ(std::equal(begin(captured), end(captured), begin(expected)),
              true);
```
Above code uses  a custom generator Source,that draws its elements from a string container. The code also demonstrate another key feature. Sink uses backpressure technique to stop fast Sources from producing data  untill the next request is made. Look at the subscribe function. It is different from what we have seen before. In this new version it accepts adding reqNext callback. The Sink supposed to call the callback when it is ready for consuming next data. The true/false argument in the callback is to tell the source that if Sink is interested in any more data.If we pass false to callback then the data propogation through chain will be stopped and finish signal will be emitted from source.  

Operators
``````
    auto m2 = Flux<std::string>::generate(
        [myvec = std::vector<std::string>{"hi", "hello"},
         i = 0](bool& hasNext) mutable {
        auto ret = myvec.at(i++);
        hasNext = i < myvec.size();
        return ret;
    });
    std::vector<int> captured;
    auto ins = std::back_inserter(captured);
    m2.filter(
          [](const auto& v) {
        return v == "hi";
    })
    .map([](auto&& v) {
          return v.length();
      })
    .subscribe([&ins](auto v, auto next) {
        *ins = v;
        next(true);
    });
    std::vector expected = {2};
    EXPECT_EQ(std::equal(begin(captured), end(captured), begin(expected)),
              true);
``````
In this example we added some operators to the chain. A filter followed by a map. As the name suggest ,the filter operator will filter out data based on the predicate supplied and the map operator will do the transformation from string to it's length. The resulting data collected at the Sink contain length of the strings that are not filtered out by the filter operation. 

So far we were talking about trivial use of frame work APIs. Now lets look at some useful Source and Sink built on top the reactor abstraction.

Http Source
``````
    net::io_context ioc;
    auto ex = net::make_strand(ioc);

    auto m2 = HttpFlux<http::string_body>::connect(
        AsyncTcpSession<http::empty_body>::create(ex),
        "https://127.0.0.1:8081/testget");

    m2.subscribe([](auto v) { EXPECT_EQ(v.response().body(), "hello"); });

    ioc.run();
``````
In this example we have an HttpFlux that carries a string response body. Under the hood HttpFlux uses an asynchronous tcp session, which represents an asynchronous tcp connection to an endpoint.
The library come with readymade HttpSession with four variation. SynchronousTcpSession, AsynchronousTcpSession , SynchronousSslSession, AsynchronousSslSession. Developer can create a HttpFlux according to their needs easily by choosing apporpriate session.Since the Source is Flux the undelying connection will be kept alive untill Sink request for the termination of data flow. If Developer interested in only single piece of data from the sever he can go for HttpMono instead.Mono will close the connection upon recieving the first response from server. 

HttSink
``````
    net::io_context ioc;
    auto ex = net::make_strand(ioc);

    auto m2 = HttpFlux<http::string_body>::connect(
        AsyncTcpSession<http::empty_body>::create(ex),
        "https://127.0.0.1:8081/testget");

    auto sink = createHttpSink<decltype(m2)::SourceType>(
        AsyncTcpSession<http::string_body>::create(ex));
    sink.setUrl("https://127.0.0.1:8081/testpost")
        .onData([](auto& res, bool& needNext) {
            EXPECT_EQ(res.response().body(), "hello");
        });

    m2.subscribe(std::move(sink));
    ioc.run();
``````
HttpFlux sources data over network. On the other hand HttSink will send data to network. In the above example data recieved from network is send back to network  using HttpSink . HttpSink will signal the response of the post request from the server through onData callback.

Broadcaster
``````
    net::io_context ioc;
    auto ex = net::make_strand(ioc);

    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);
    auto m2 = HttpFlux<http::string_body>::connect(
        AsyncSslSession<http::empty_body>::create(ex, ctx),
        "https://127.0.0.1:8443/testget");

    auto sink1 = createHttpSink<decltype(m2)::SourceType>(
        AsyncSslSession<http::string_body>::create(ex, ctx));
    sink1.setUrl("https://127.0.0.1:8443/testpost")
        .onData([](auto& res, bool& needNext) {
            EXPECT_EQ(res.response().body(), "hello");
        });

    auto sink2 = createHttpSink<decltype(m2)::SourceType>(
        AsyncSslSession<http::string_body>::create(ex, ctx));
    sink2.setUrl("https://127.0.0.1:8443/testpost")
        .onData([i = 0](auto& res, bool& needNext) mutable {
            if (!res.isError())
            {
                EXPECT_EQ(res.response().body(), "hello");
                if (i++ < 5)
                    needNext = true;
                return;
            }
            std::cout << res.error().what() << "\n" << res.response();
        });

    m2.subscribe(
        createStringBodyBroadCaster(std::move(sink1), std::move(sink2)));

    ioc.run();
``````
The above example demonstrate the use of Broadcasting Sink. Here we are creating multiple HttpSinks and group them together to form a Broadcasting Sink.As a result the data from the source will be send to all endpoints where each individual Sinks points to. This example also demonstrates the usage of AsyncSslSession session. Note that there is no need for the Sourece to be always a HttpSink in order to use HttpSink. It could be any generator including Flux/Mono with values or cutom function etc. The followig code demostrate the same
``````
    net::io_context ioc;
    auto ex = net::make_strand(ioc);

    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);

    auto m2 = Flux<std::string>::generate([i = 1](bool& hasNext) mutable {
        std::string ret("hello ");
        ret += std::to_string(i++);
        return ret;
    });
    auto sink2 = createHttpSink<std::string>(
        AsyncSslSession<http::string_body>::create(ex, ctx));
    int i = 1;
    sink2.setUrl("https://127.0.0.1:8443/testpost")
        .onData([&i](const auto& res, bool& needNext) mutable {
            if (!res.isError())
            {
                std::string expected("hello ");
                expected += std::to_string(i++);
                EXPECT_EQ(res.response().body(), expected);
                std::cout << res.response().body() << "\n";
                if (i < 5)
                    needNext = true;
                return;
            }
            std::cout << res.error().what() << "\n" << res.response();
        });
    m2.subscribe(std::move(sink2));
    ioc.run();
    
``````
We have been talking about different types of HttpSource and Sinks. Now lets look at  Dbus example

Converting to Dbus Managed Object Tree to Json
``````
  ManagedObjectType resp = ...;//get managed objects from dbus call

  auto treeGen = reactor::Flux<DbusTreeGenerator::value_type>::generate(
      DbusTreeGenerator(resp));
  auto jsonstr =
      treeGen
          .filter([](const DbusTreeGenerator::value_type &v) {
            return std::get<0>(v) == "/xyz/openbmc_project/license/entry2/";
          })
          .map([](const DbusTreeGenerator::value_type &v) {
            return std::visit(JsonConverter{std::get<2>(v)}, std::get<3>(v));
          })
          .to(JsonCollector())
          .toJson()
          .dump(4);
  std::cout << jsonstr << "\n";

``````
Above example demonstrate convertion process from tree of Dbus objects in to a Json string.
DbusTreeGenerator is genertor function the takes a managed object tree as argument.
The Flux created from DbusTreeGenerator undergoes several transformations before it reaches
the JsonCollector, which is a Sink.The tranformation include certain filter to avoid collecting data for certain Dbus objects,then a transformation that tranform tuple of {Path,IfaceName,PropName,DbusVariant} in to a Json node. Finally a JsonCollector sink that accumulate all json node in to summary json. The resulting Json can be retrieved from the collector using toJson function.

There are several other Sources and Sinks can be implemented using the reactor frame work. I think above example suffice to convey the usefullness of the framework.
## Alternatives Considered

In BmcWeb implementation you could see alternate approach taken for solving http broadcasting and Dbus tree conversions. Hopefully, the reactor approach will simplify the code, improves the readability, maintainability and enables concurrency.

## Impacts

The implementation does not have any impact on current OpenBmc components.It is presents an alternative approach for solving existing OpenBmc problems.  

### Organizational

The library need a new repository in OpenBmc. 
Mainainers : Abhilash Raju (abhilash.kollam@gmail.com)
At present there is no change required to any other repo. 

## Testing
Unit test based on google test is the preferred testing method. 
So it is easy to integrate with CI.