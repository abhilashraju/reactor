mono_test_sources = [
    'mono_test.cpp'
]

mono_test_deps = [
json_dep,boost_dep
]



mono_test = executable('mono_test', 
    [mono_test_sources, test_main], 
    include_directories : core_includes,
    dependencies : [mono_test_deps,test_deps,openssl_dep], 
    cpp_args : REACTOR_ARGS,
    link_with : [ test_dep_libs])

test('mono test', mono_test)

all_test_deps += mono_test_deps
# all_test_dep_libs += mono_test_lib_deps
# project_benchmark_sources += files(mono_test_sources)