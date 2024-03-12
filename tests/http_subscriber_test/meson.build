http_subscriber_test_sources = [
    'http_subscriber_test.cpp'
]

http_subscriber_test_deps = [
json_dep,boost_dep
]



http_subscriber_test = executable('http_subscriber_test', 
    [http_subscriber_test_sources, test_main], 
    include_directories : core_includes,
    dependencies : [http_subscriber_test_deps,test_deps,openssl_dep], 
    link_with : [ test_dep_libs])

test('http_subscriber test', http_subscriber_test)

all_test_deps += http_subscriber_test_deps
# all_test_dep_libs += http_subscriber_test_lib_deps
# project_benchmark_sources += files(http_subscriber_test_sources)