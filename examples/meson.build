
boost_dep = dependency('boost',modules: ['filesystem'])
plugin_includes = include_directories(['plugins/redfish_events'])
executable('bmcapp', 'bmcapp.cpp',
           include_directories : [core_includes,plugin_includes],
           dependencies : [boost_dep],
           install : true)