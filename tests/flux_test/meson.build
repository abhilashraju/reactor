flux_test_sources = [
    'flux_test.cpp'
]

flux_test_deps = [
json_dep,boost_dep
]



flux_test = executable('flux_test', 
    [flux_test_sources, test_main], 
    include_directories : core_includes,
    dependencies : [flux_test_deps,test_deps,openssl_dep], 
    link_with : [ test_dep_libs])

test('flux test', flux_test)

all_test_deps += flux_test_deps
# all_test_dep_libs += flux_test_lib_deps
# project_benchmark_sources += files(flux_test_sources)