add_library(cgraph_mcp STATIC
  mcp_server.cpp)

add_library(cgraph::mcp ALIAS cgraph_mcp)

target_include_directories(cgraph_mcp
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(cgraph_mcp
  PUBLIC
    cgraph::engine
    nlohmann_json::nlohmann_json)

cgraph_set_warnings(cgraph_mcp)
cgraph_enable_sanitizers(cgraph_mcp)

add_executable(cgraph-mcp
  main.cpp)

target_link_libraries(cgraph-mcp
  PRIVATE
    cgraph::mcp
    cgraph::engine)

cgraph_set_warnings(cgraph-mcp)
cgraph_enable_sanitizers(cgraph-mcp)
