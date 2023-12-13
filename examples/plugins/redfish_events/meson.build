redfish_event_plugin_sources = [
    'redfish_event.cpp'
]


redfish_event_plugin_deps = [
json_dep,boost_dep,openssl_dep
]



redfish_event = library('redfish_event_plugin', 
    sources : redfish_event_plugin_sources,
    include_directories : core_includes,
    dependencies : redfish_event_plugin_deps,
    cpp_args : REACTOR_ARGS,
    link_with : [])
    



# all_test_dep_libs += flux_test_lib_deps
# project_benchmark_sources += files(flux_test_sources)