project(
  'reactor',
  'cpp',
  version: '0.1',
  meson_version: '>=0.58.0',
  default_options: [
    'warning_level=3',
    'cpp_std=c++23',
  ])


add_project_arguments('-DSSL_ON', language : 'cpp')
openssl_dep = dependency('openssl', version: '>=1.1.1')
boost_dep = dependency('boost',modules: ['coroutine','url'])
json_dep = dependency('nlohmann_json')
gtest = subproject('gtest')

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

