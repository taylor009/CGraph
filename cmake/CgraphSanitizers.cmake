function(cgraph_enable_sanitizers target)
  if(NOT CGRAPH_ENABLE_SANITIZERS)
    return()
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE /fsanitize=address)
    return()
  endif()

  target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()
