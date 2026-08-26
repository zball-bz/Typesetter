cmake_minimum_required(VERSION 3.20)
project(typesetter C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(tsr_core STATIC
  src/hyphen/hyphen.cc
  src/linepass/linepass.cc
  src/inline/inline.cc
  src/inline/fragment.cc
  src/codegen/codegen.cc
  src/ops/ops.cc
  src/model/model.cc
  src/code/tokens.cc
  src/math/math.cc
  src/resolve/resolve.cc
  src/emit/emit.cc
  src/break/break.cc
  src/layout/layout.cc
  src/render/typeset_html.cc
  src/render/semantic_html.cc
)
target_compile_options(tsr_core PUBLIC -fno-exceptions -fno-rtti -Wall -Wextra)
target_include_directories(tsr_core PUBLIC src)

if(EMSCRIPTEN)
  add_executable(typesetter src/api/wasm_api.cc)
  target_link_libraries(typesetter tsr_core)
  target_link_options(typesetter PRIVATE
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=worker,node
    -sALLOW_MEMORY_GROWTH=1
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,stringToNewUTF8,UTF8ToString,lengthBytesUTF8
    -sEXPORT_NAME=createTypesetter)
  set_target_properties(typesetter PROPERTIES OUTPUT_NAME "typesetter")
else()
  # statically linked tree-sitter + grammars: the native token provider
  # (code-design.md §2 — same parse tables as the web side modules)
  add_library(tsr_treesitter STATIC
    ../third_party/tree-sitter/lib/src/lib.c
    ../third_party/grammars/json/parser.c
  )
  target_include_directories(tsr_treesitter PUBLIC
    ../third_party/tree-sitter/lib/include
    ../third_party/tree-sitter/lib/src
    ../third_party/grammars/json
  )
  target_compile_options(tsr_treesitter PRIVATE -w)

  add_executable(tsrc src/api/native_cli.cc)
  target_link_libraries(tsrc tsr_core tsr_treesitter)
  target_compile_definitions(tsrc PRIVATE
    TSR_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

  add_executable(tsr_tests test/tests.cc)
  target_link_libraries(tsr_tests tsr_core tsr_treesitter)
  target_compile_definitions(tsr_tests PRIVATE
    TSR_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

  enable_testing()
  add_test(NAME engine COMMAND tsr_tests ${CMAKE_CURRENT_SOURCE_DIR}/..)

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tsr_core PUBLIC -fsanitize=address,undefined -g)
    target_link_options(tsrc PRIVATE -fsanitize=address,undefined)
    target_link_options(tsr_tests PRIVATE -fsanitize=address,undefined)
  endif()
endif()
