http_client_test_sources = [
    'http_client_test.cpp'
]

http_client_test_deps = [
json_dep,boost_dep
]



http_client_test = executable('http_client_test', 
    [http_client_test_sources, test_main], 
    include_directories : core_includes,
    dependencies : [http_client_test_deps,test_deps,openssl_dep], 
    cpp_args : REACTOR_ARGS,
    link_with : [ test_dep_libs])

test('http_client test', http_client_test)

all_test_deps += http_client_test_deps
# all_test_dep_libs += http_client_test_lib_deps
# project_benchmark_sources += files(http_client_test_sources)