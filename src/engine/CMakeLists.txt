add_library(cgraph_engine STATIC
  analysis.cpp
  daemon_endpoint.cpp
  daemon_server.cpp
  configured_extractors.cpp
  daemon_lifecycle.cpp
  daemon_ops.cpp
  daemon_security.cpp
  daemon_identity.cpp
  dedup.cpp
  detect.cpp
  engine.cpp
  extractor.cpp
  export_json.cpp
  file_cache.cpp
  file_extraction.cpp
  file_watcher.cpp
  fragment_json.cpp
  graph_builder.cpp
  incremental_update.cpp
  javascript_extractor.cpp
  language_config.cpp
  non_grammar_extractors.cpp
  normalize.cpp
  parser_pool.cpp
  path_ignore.cpp
  pipeline.cpp
  protocol.cpp
  python_extractor.cpp
  semantic_chunk_plan.cpp
  semantic_cache.cpp
  semantic_drop.cpp
  semantic_fragment_validation.cpp
  semantic_ingest.cpp
  semantic_orchestration.cpp
  tsconfig_aliases.cpp)

add_library(cgraph::engine ALIAS cgraph_engine)

target_include_directories(cgraph_engine
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(cgraph_engine
  PUBLIC
    nlohmann_json::nlohmann_json
    cgraph_tree_sitter
  PRIVATE
    CURL::libcurl
    igraph::igraph
    utf8proc::utf8proc)

cgraph_set_warnings(cgraph_engine)
cgraph_enable_sanitizers(cgraph_engine)
