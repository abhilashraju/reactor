all_test_deps = []
all_test_dep_libs = []
project_test_sources = []

gtest_dep = gtest.get_variable('gtest_dep')
chaibeast_dep=chaibeast.get_variable('chaibeast_dep')
# thread_dep = dependency('threads')
test_main = files('tests_main.cc')

test_deps = [
    gtest_dep,chaibeast_dep
]

test_dep_libs = [

]

subdir('mono_test')
subdir('flux_test')
subdir('webclient_test')
subdir('http_client_test')
subdir('http_subscriber_test')

# This executable contains all the tests
project_test_sources += test_main
all_test_deps += test_deps
all_test_dep_libs += test_dep_libs
CHAI_ARGS=['-DSSL_ON']
all_testes = executable('all_tests', 
    project_test_sources, 
    include_directories : core_includes, 
    cpp_args:CHAI_ARGS,
    dependencies : all_test_deps, 
    link_with : all_test_dep_libs)
