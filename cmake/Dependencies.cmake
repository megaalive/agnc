find_package(yyjson CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

function(agnc_link_dependencies target_name)
    target_link_libraries(${target_name} PRIVATE yyjson::yyjson CURL::libcurl unofficial::sqlite3::sqlite3)
endfunction()
