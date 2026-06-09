add_library(cgraph_client_runtime STATIC
  client_runtime.cpp)

add_library(cgraph::client_runtime ALIAS cgraph_client_runtime)

target_include_directories(cgraph_client_runtime
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(cgraph_client_runtime
  PUBLIC
    cgraph::engine
    nlohmann_json::nlohmann_json)

cgraph_set_warnings(cgraph_client_runtime)
cgraph_enable_sanitizers(cgraph_client_runtime)

add_executable(cgraph-client
  main.cpp)

target_link_libraries(cgraph-client
  PRIVATE
    cgraph::client_runtime
    cgraph::engine)

cgraph_set_warnings(cgraph-client)
cgraph_enable_sanitizers(cgraph-client)
