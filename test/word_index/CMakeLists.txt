# Host tests for lib/WordIndex (task R3). Two executables:
#   WordIndexTest        the library against the firmware's own lib/expat, built with the firmware's
#                        expat flags, under AddressSanitizer and UndefinedBehaviorSanitizer: word
#                        rules, the fixture book, the file format and its robustness, the RsvpCore
#                        adapter.
#   WordIndexParityTest  the library against the real, unmodified ChapterHtmlSlimParser (system expat,
#                        as test/chapter_html_slim_parser): offsets and page mapping, token by token.

set(WORD_INDEX_DIR ${REPO_ROOT}/lib/WordIndex)
set(WORD_INDEX_SOURCES
  ${WORD_INDEX_DIR}/WordIndexFormat.cpp
  ${WORD_INDEX_DIR}/WordIndexBuilder.cpp
  ${WORD_INDEX_DIR}/WordIndexReader.cpp
)
set(WORD_INDEX_INCLUDES
  ${WORD_INDEX_DIR}
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/XmlParserUtils
)
set(FIRMWARE_EXPAT_SOURCES
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)
# The firmware's expat build flags (platformio.ini).
set(FIRMWARE_EXPAT_DEFINITIONS XML_GE=0 XML_CONTEXT_BYTES=0)

# Test inputs extracted into the build tree: the fixture's spine documents, the fixture prepared
# by tools/prep (its anchors), and one 3,051-word chapter of test/epubs.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(WIX_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
add_custom_command(
  OUTPUT ${WIX_DATA_DIR}/sources.txt
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_test_data.py ${REPO_ROOT} ${WIX_DATA_DIR}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/prepare_test_data.py
    ${REPO_ROOT}/fixtures/books/fixture.epub
    ${REPO_ROOT}/fixtures/books/fixture.draft.json
    ${REPO_ROOT}/fixtures/books/fixture.expected.json
    ${REPO_ROOT}/test/epubs/test_reader_menu_progress.epub
  COMMENT "Extracting WordIndex test chapters and preparing the fixture book"
  VERBATIM
)
add_custom_target(WordIndexTestData DEPENDS ${WIX_DATA_DIR}/sources.txt)

set(WIX_TEST_DEFINITIONS
  WIX_DATA_DIR="${WIX_DATA_DIR}"
  WIX_SPIKE_FIXTURE="${REPO_ROOT}/test/spike_word_stream/fixture_s1.xhtml"
  RSVP_REPO_ROOT="${REPO_ROOT}"
  RSVP_TEST_DIR="${REPO_ROOT}/test/rsvp_core"
)

# Compile-only check that the library builds without exceptions or RTTI, warning-free, against the
# firmware's expat headers and flags.
add_library(WordIndexNoRttiCheck OBJECT ${WORD_INDEX_SOURCES} ${WORD_INDEX_DIR}/IndexWordSource.cpp)
target_include_directories(WordIndexNoRttiCheck PRIVATE
  ${WORD_INDEX_INCLUDES} ${REPO_ROOT}/lib/expat ${REPO_ROOT}/lib/RsvpCore)
target_compile_definitions(WordIndexNoRttiCheck PRIVATE ${FIRMWARE_EXPAT_DEFINITIONS})
target_compile_options(WordIndexNoRttiCheck PRIVATE
  -fno-exceptions -fno-rtti -Wall -Wextra -pedantic -Wconversion -Wshadow -Werror)

set(RSVP_CORE_DIR ${REPO_ROOT}/lib/RsvpCore)
add_executable(WordIndexTest
  WordIndexTestSupport.cpp
  WordRulesTest.cpp
  FixtureMatchTest.cpp
  FormatTest.cpp
  RobustnessTest.cpp
  AdapterTest.cpp
  ${WORD_INDEX_SOURCES}
  ${WORD_INDEX_DIR}/IndexWordSource.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${FIRMWARE_EXPAT_SOURCES}
  ${RSVP_CORE_DIR}/RsvpText.cpp
  ${RSVP_CORE_DIR}/WordClassifier.cpp
  ${RSVP_CORE_DIR}/FocusLetter.cpp
  ${RSVP_CORE_DIR}/Pacing.cpp
  ${RSVP_CORE_DIR}/FrameBuilder.cpp
  ${RSVP_CORE_DIR}/ClearingPolicy.cpp
  ${RSVP_CORE_DIR}/DeliveredWpmMeter.cpp
  ${RSVP_CORE_DIR}/WordOrderAudit.cpp
  ${RSVP_CORE_DIR}/Scheduler.cpp
)
add_dependencies(WordIndexTest WordIndexNoRttiCheck WordIndexTestData)
target_include_directories(WordIndexTest PRIVATE
  ${WORD_INDEX_INCLUDES}
  ${REPO_ROOT}/lib/expat
  ${RSVP_CORE_DIR}
  ${REPO_ROOT}/test/rsvp_core
)
target_compile_definitions(WordIndexTest PRIVATE ${FIRMWARE_EXPAT_DEFINITIONS} ${WIX_TEST_DEFINITIONS})

# AddressSanitizer and UndefinedBehaviorSanitizer for this target, as test/checkpoint_core. Any
# report aborts the test. Set WORD_INDEX_SANITIZE=OFF to build without.
option(WORD_INDEX_SANITIZE "Build WordIndexTest with ASan and UBSan" ON)
if(WORD_INDEX_SANITIZE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(WordIndexTest PRIVATE
    -fsanitize=address,undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
  )
  target_link_options(WordIndexTest PRIVATE -fsanitize=address,undefined)
endif()

target_link_libraries(WordIndexTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WordIndexTest)

# Renderer parity: the real, unmodified page renderer (upstream sources, the host stubs of
# test/chapter_html_slim_parser, real entities and English hyphenation) and WordIndex in one binary,
# both on system expat as the other parser tests.
set(PARSER_STUBS ${REPO_ROOT}/test/chapter_html_slim_parser/stubs)
add_executable(WordIndexParityTest
  WordIndexParityTest.cpp
  WordIndexTestSupport.cpp
  ParityLinkStubs.cpp
  ${WORD_INDEX_SOURCES}
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
add_dependencies(WordIndexParityTest WordIndexTestData)
target_include_directories(WordIndexParityTest PRIVATE
  ${PARSER_STUBS}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers
  ${WORD_INDEX_INCLUDES}
)
target_compile_definitions(WordIndexParityTest PRIVATE ${WIX_TEST_DEFINITIONS})
target_link_libraries(WordIndexParityTest PRIVATE crosspoint_test_common GTest::gtest_main expat)
gtest_discover_tests(WordIndexParityTest)
