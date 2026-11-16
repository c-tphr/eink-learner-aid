# THROWAWAY PROTOTYPE for spike S1 (issue #9): word stream and page-to-word mapping.
# Deliberately a standalone CMake project, not wired into test/CMakeLists.txt, so it adds nothing
# to the host suite and changes no upstream file. Delete once lib/WordIndex exists (task R3).
#
#   cmake -S test/spike_word_stream -B build/spike_word_stream
#   cmake --build build/spike_word_stream -j4
#   build/spike_word_stream/SpikeWordStreamTest
#
# It compiles upstream sources unmodified, with the host stubs of test/chapter_html_slim_parser.
cmake_minimum_required(VERSION 3.16)
project(spike_word_stream C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(PARSER_STUBS "${REPO_ROOT}/test/chapter_html_slim_parser/stubs")

# One real multi-page chapter from an EPUB already in test/epubs, extracted at configure time.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SPIKE_EPUB "${REPO_ROOT}/test/epubs/test_reader_menu_progress.epub")
set(SPIKE_EPUB_CHAPTER "${CMAKE_CURRENT_BINARY_DIR}/test_reader_menu_progress_chapter1.xhtml")
execute_process(
  COMMAND ${Python3_EXECUTABLE} -I -c
          "import sys, zipfile; open(sys.argv[3], 'wb').write(zipfile.ZipFile(sys.argv[1]).read(sys.argv[2]))"
          ${SPIKE_EPUB} OEBPS/chapter1.xhtml ${SPIKE_EPUB_CHAPTER}
  COMMAND_ERROR_IS_FATAL ANY
)

add_executable(SpikeWordStreamTest
  SpikeWordStreamTest.cpp
  WordStreamSpike.cpp
  SpikeLinkStubs.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/WordStore.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/CjkBreaks.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(SpikeWordStreamTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PARSER_STUBS}
  ${REPO_ROOT}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/XmlParserUtils
)

target_compile_definitions(SpikeWordStreamTest PRIVATE
  SPIKE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixture_s1.xhtml"
  SPIKE_EPUB_CHAPTER="${SPIKE_EPUB_CHAPTER}"
)

target_link_libraries(SpikeWordStreamTest PRIVATE GTest::gtest_main expat)

enable_testing()
add_test(NAME SpikeWordStreamTest COMMAND SpikeWordStreamTest)
