find_package(yyjson CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)

function(agnc_link_dependencies target_name)
    target_link_libraries(${target_name} PRIVATE yyjson::yyjson CURL::libcurl)
endfunction()
