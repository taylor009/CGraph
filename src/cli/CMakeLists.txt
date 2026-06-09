add_executable(cgraph
  main.cpp)

target_link_libraries(cgraph
  PRIVATE
    cgraph::engine)

cgraph_set_warnings(cgraph)
cgraph_enable_sanitizers(cgraph)
