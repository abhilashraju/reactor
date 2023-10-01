webclient_sources = [
    'webclient_test.cpp'
]

webclient_deps = [
json_dep,boost_dep
]



webclient = executable('webclient', 
    [webclient_sources, test_main], 
    include_directories : core_includes,
    dependencies : [webclient_deps,test_deps,openssl_dep], 
    cpp_args : REACTOR_ARGS,
    link_with : [ test_dep_libs])

test('flux test', webclient)

all_test_deps += webclient_deps
# all_test_dep_libs += webclient_lib_deps
# project_benchmark_sources += files(webclient_sources)