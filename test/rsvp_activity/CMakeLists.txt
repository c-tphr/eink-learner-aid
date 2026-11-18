# Host tests for the pure parts of the RSVP activity (task R4a): the control map and its gesture
# reader (src/controls/), the word-band layout (src/activities/rsvp/WordBand), and the reading helpers
# (src/reading/): index store decisions, page/word hand-over and sentence stepping, on real indexes
# of the fixture book built by lib/WordIndex. Test inputs come from test/word_index's data step.
# Task R4b adds the pace settings and the state-file rules (src/reading/RsvpValues, BookStateRules),
# which use lib/CheckpointCore's state codec.

set(RSVP_CORE_DIR ${REPO_ROOT}/lib/RsvpCore)
set(WORD_INDEX_DIR ${REPO_ROOT}/lib/WordIndex)
set(CHECKPOINT_CORE_DIR ${REPO_ROOT}/lib/CheckpointCore)
set(RSVP_ACTIVITY_SOURCES
  ${REPO_ROOT}/src/controls/ControlMap.cpp
  ${REPO_ROOT}/src/controls/ControlGesture.cpp
  ${REPO_ROOT}/src/activities/rsvp/WordBand.cpp
  ${REPO_ROOT}/src/reading/WordIndexCheck.cpp
  ${REPO_ROOT}/src/reading/RsvpPosition.cpp
  ${REPO_ROOT}/src/reading/RsvpValues.cpp
  ${REPO_ROOT}/src/reading/BookStateRules.cpp
)

add_executable(RsvpActivityTest
  ControlMapTest.cpp
  ControlGestureTest.cpp
  WordBandTest.cpp
  ReadingHelpersTest.cpp
  RsvpSettingsTest.cpp
  ${RSVP_ACTIVITY_SOURCES}
  ${REPO_ROOT}/test/word_index/WordIndexTestSupport.cpp
  ${WORD_INDEX_DIR}/WordIndexFormat.cpp
  ${WORD_INDEX_DIR}/WordIndexBuilder.cpp
  ${WORD_INDEX_DIR}/WordIndexReader.cpp
  ${WORD_INDEX_DIR}/IndexWordSource.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
  ${RSVP_CORE_DIR}/RsvpText.cpp
  ${RSVP_CORE_DIR}/WordClassifier.cpp
  ${RSVP_CORE_DIR}/FocusLetter.cpp
  ${RSVP_CORE_DIR}/SentenceStep.cpp
  ${CHECKPOINT_CORE_DIR}/BookState.cpp
  ${CHECKPOINT_CORE_DIR}/GradeTable.cpp
  ${CHECKPOINT_CORE_DIR}/StateCodec.cpp
  ${CHECKPOINT_CORE_DIR}/JsonText.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointTypes.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointIndex.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointParser.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointSource.cpp
)
add_dependencies(RsvpActivityTest WordIndexTestData)

target_include_directories(RsvpActivityTest PRIVATE
  ${REPO_ROOT}/src
  ${RSVP_CORE_DIR}
  ${WORD_INDEX_DIR}
  ${CHECKPOINT_CORE_DIR}
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/test/rsvp_core
)

target_compile_definitions(RsvpActivityTest PRIVATE
  CROSSPOINT_RSVP=1
  XML_GE=0
  XML_CONTEXT_BYTES=0
  WIX_DATA_DIR="${CMAKE_BINARY_DIR}/word_index/data"
  RSVP_REPO_ROOT="${REPO_ROOT}"
  RSVP_TEST_DIR="${REPO_ROOT}/test/rsvp_core"
)

# The firmware code under test builds without exceptions or RTTI, warning-free.
set_source_files_properties(${RSVP_ACTIVITY_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-fno-rtti;-Wshadow;-Werror"
)

target_link_libraries(RsvpActivityTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RsvpActivityTest)
