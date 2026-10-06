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
  src/syntax/syntax.gen.cc
  src/syntax/lexer.cc
  src/syntax/exports.cc
  src/elements/registry.cc
  src/semantic/locale.cc
  src/semantic/manifest.cc
  src/semantic/terms.cc
  src/semantic/numbering.cc
  src/semantic/index.cc
  src/semantic/materialize.cc
  src/semantic/declare.cc
  src/codegen/codegen.cc
  src/codegen/lower.cc
  src/ops/ops.cc
  src/ops/schema.gen.cc
  src/ops/domains.gen.cc
  src/api/settings.gen.cc
  src/model/model.cc
  src/model/cascade.cc
  src/model/normalize.cc
  src/code/overlay.cc
  src/code/tokens.cc
  src/math/math.cc
  src/math/font.cc
  src/math/ir.cc
  src/math/env.cc
  src/resolve/resolve.cc
  src/resource/box.cc
  src/resource/codec.cc
  src/resource/resource_table.cc
  src/resource/session.cc
  src/shape/hlist.cc
  src/shape/objects.cc
  src/emit/emit.cc
  src/emit/legacy.cc
  src/break/break.cc
  src/boxtree/build.cc
  src/layout/layout.cc
  src/layout/grid.cc
  src/layout/paginate.cc
  src/paint/paint.cc
  src/render/math_html.cc
  src/render/typeset_html.cc
  src/render/rules_css.cc
  src/render/semantic_html.cc
)
# built-in element rows and locale packs, embedded (plan P1-10)
set(TSR_DATA data/elements.json data/defaults.json data/locale/en.json data/locale/zh-Hans.json
    data/locale/zh-Hant.json data/locale/ja.json data/locale/ko.json data/locale/locale.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TSR_DATA})
file(READ data/elements.json ELEMENTS_JSON)
file(READ data/defaults.json DEFAULTS_JSON)
file(READ data/locale/en.json LOCALE_EN)
file(READ data/locale/zh-Hans.json LOCALE_ZH_HANS)
file(READ data/locale/zh-Hant.json LOCALE_ZH_HANT)
file(READ data/locale/ja.json LOCALE_JA)
file(READ data/locale/ko.json LOCALE_KO)
file(READ data/locale/locale.json LOCALE_TABLE)
configure_file(src/semantic/semantic_data.gen.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/gen/semantic_data.gen.h @ONLY)
target_include_directories(tsr_core PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_compile_options(tsr_core PUBLIC -fno-exceptions -fno-rtti -Wall -Wextra -Werror=switch)
# no fused multiply-add: layout numbers must not depend on the target's FMA
# (native goldens vs the WASM build; plan P0-12, design T6 I9)
target_compile_options(tsr_core PRIVATE -ffp-contract=off)
# front end: every enumerator handled explicitly, no default: arms (plan P0-02)
set_source_files_properties(
  src/linepass/linepass.cc src/inline/inline.cc
  src/syntax/syntax.gen.cc src/syntax/lexer.cc src/syntax/exports.cc
  src/codegen/codegen.cc
  PROPERTIES COMPILE_OPTIONS "-Wswitch-enum;-Werror=switch-enum")
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
  # test-only build with the parity-check exports (tools/wasm-goldens.mjs)
  add_executable(typesetter_debug src/api/wasm_debug.cc)
  target_link_libraries(typesetter_debug tsr_core)
  target_link_options(typesetter_debug PRIVATE
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node
    -sALLOW_MEMORY_GROWTH=1
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,stringToNewUTF8,UTF8ToString,lengthBytesUTF8
    -sEXPORT_NAME=createTypesetterDebug)
else()
  # statically linked tree-sitter + grammars: the native token provider
  # (code-design.md §2 — same parse tables as the web side modules). The
  # grammars are engine/schema/languages.json's native ones (plan P3-22:
  # tools/gen-languages.mjs writes native_grammars.gen.cmake).
  include(${CMAKE_CURRENT_SOURCE_DIR}/native_grammars.gen.cmake)
  set(TSR_GRAMMAR_SOURCES)
  set(TSR_GRAMMAR_DIRS)
  set(TSR_QUERIES "")
  set(TSR_GRAMMAR_ROWS "")
  foreach(g ${TSR_NATIVE_GRAMMARS})
    foreach(f ${TSR_GRAMMAR_${g}_FILES})
      list(APPEND TSR_GRAMMAR_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../${TSR_GRAMMAR_${g}_DIR}/${f})
    endforeach()
    list(APPEND TSR_GRAMMAR_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../${TSR_GRAMMAR_${g}_DIR})
    # its highlight queries, embedded at build time (plan P1-03: tsrc needs
    # no repository path), base first
    set(scm "")
    foreach(q ${TSR_GRAMMAR_${g}_SCM})
      file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../${q} part)
      string(APPEND scm "${part}\n")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../${q})
    endforeach()
    string(APPEND TSR_QUERIES "extern \"C\" const TSLanguage* tree_sitter_${g}();\n")
    string(APPEND TSR_QUERIES "constexpr std::string_view kQuery_${g} = R\"tsrq(${scm})tsrq\";\n")
    string(APPEND TSR_GRAMMAR_ROWS "    {\"${g}\", tree_sitter_${g}, kQuery_${g}},\n")
  endforeach()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/native_grammars.gen.cmake)
  add_library(tsr_treesitter STATIC
    ../third_party/tree-sitter/lib/src/lib.c
    ${TSR_GRAMMAR_SOURCES}
  )
  target_include_directories(tsr_treesitter PUBLIC
    ../third_party/tree-sitter/lib/include
    ../third_party/tree-sitter/lib/src
    ${TSR_GRAMMAR_DIRS}
  )
  target_compile_options(tsr_treesitter PRIVATE -w)

  # the native token provider and its grammar table
  configure_file(src/code/native_queries.gen.h.in
                 ${CMAKE_CURRENT_BINARY_DIR}/gen/native_queries.gen.h @ONLY)
  add_library(tsr_native STATIC src/code/native_tokens.cc)
  target_include_directories(tsr_native PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)
  target_link_libraries(tsr_native tsr_core tsr_treesitter)

  add_executable(tsrc src/api/native_cli.cc)
  target_link_libraries(tsrc tsr_native tsr_core tsr_treesitter)

  add_executable(tsr_tests test/tests.cc)
  target_link_libraries(tsr_tests tsr_native tsr_core tsr_treesitter)

  enable_testing()
  add_test(NAME engine COMMAND tsr_tests ${CMAKE_CURRENT_SOURCE_DIR}/..)

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # -fno-sanitize-recover: a UBSan report must fail the test run (gate G2)
    target_compile_options(tsr_core PUBLIC -fsanitize=address,undefined
                           -fno-sanitize-recover=all -g)
    target_link_options(tsrc PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
    target_link_options(tsr_tests PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
  endif()
endif()

# libFuzzer targets (plan P0-03): configure a separate clang build with
#   CC=clang CXX=clang++ cmake -S engine -B engine/build-fuzz -DTSR_FUZZ=ON
option(TSR_FUZZ "build libFuzzer targets (clang only)" OFF)
if(TSR_FUZZ AND NOT EMSCRIPTEN)
  foreach(t fuzz_linepass fuzz_inline fuzz_opreader fuzz_settings fuzz_resanswer fuzz_lower fuzz_fragment fuzz_inputs)
    add_executable(${t} fuzz/${t}.cc)
    target_link_libraries(${t} tsr_core)
    target_compile_options(${t} PRIVATE -fsanitize=fuzzer,address,undefined
                           -fno-sanitize-recover=all)
    target_link_options(${t} PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
  endforeach()
  target_compile_options(tsr_core PUBLIC -fsanitize=fuzzer-no-link,address,undefined
                         -fno-sanitize-recover=all -g)
endif()
