add_executable(graphd
  main.cpp)

target_link_libraries(graphd
  PRIVATE
    cgraph::engine)

cgraph_set_warnings(graphd)
cgraph_enable_sanitizers(graphd)
