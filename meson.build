project(
  'reactor',
  'cpp',
  version: '0.1',
  meson_version: '>=0.58.0',
  default_options: [
    'warning_level=3',
    'cpp_std=c++23',
  ])

#REACTOR_ARGS=['-DBOOST_URL_NO_SOURCE_LOCATION']

openssl_dep = dependency('openssl', version: '>=1.1.1')
boost_dep = dependency('boost',modules: ['coroutine','url'])
json_dep = dependency('nlohmann_json')
gtest = subproject('gtest')
chaibeast=subproject('chaibeast')
#benchmark = subproject('benchmark')

core_includes = include_directories(['./include'])
reactor_dep = declare_dependency(
	include_directories : core_includes,
	dependencies : [dependency('threads'),openssl_dep,boost_dep,json_dep]
)
subdir('examples')
if get_option('enable-tests')
  subdir('tests')
endif

