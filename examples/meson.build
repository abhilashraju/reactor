plugin_includes=['plugins/redfish_events']

# Specify the dependency when building the executable
executable('bmcapp', 'bmcapp.cpp',
           include_directories : [core_includes,plugin_includes],
           dependencies : [boost_dep],
           install : true)