# Host tests for the pure parts of the checkpoint screens and their data loader (task C3a): the
# screens' items, defaults and paging (src/activities/checkpoint/CheckpointScreens), the state file
# edits and grade time (CheckpointSave), the vertical swipe of the gesture reader (src/controls/), the
# OPF manifest lookup (src/reading/OpfItemFinder) and the loader's decisions on the shared samples of
# fixtures/checkpoints (src/reading/CheckpointBookRules).

set(CHECKPOINT_CORE_DIR ${REPO_ROOT}/lib/CheckpointCore)
set(CHECKPOINT_ACTIVITY_SOURCES
  ${REPO_ROOT}/src/controls/ControlMap.cpp
  ${REPO_ROOT}/src/controls/ControlGesture.cpp
  ${REPO_ROOT}/src/activities/checkpoint/CheckpointScreens.cpp
  ${REPO_ROOT}/src/activities/checkpoint/CheckpointSave.cpp
  ${REPO_ROOT}/src/reading/OpfItemFinder.cpp
  ${REPO_ROOT}/src/reading/CheckpointBookRules.cpp
)

add_executable(CheckpointActivityTest
  CheckpointScreensTest.cpp
  CheckpointSaveTest.cpp
  OpfItemFinderTest.cpp
  CheckpointBookRulesTest.cpp
  ${CHECKPOINT_ACTIVITY_SOURCES}
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
  ${CHECKPOINT_CORE_DIR}/BookState.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointFlow.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointIndex.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointParser.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointSource.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointTrigger.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointTypes.cpp
  ${CHECKPOINT_CORE_DIR}/GradeTable.cpp
  ${CHECKPOINT_CORE_DIR}/JsonText.cpp
  ${CHECKPOINT_CORE_DIR}/StateCodec.cpp
  ${CHECKPOINT_CORE_DIR}/StateRestore.cpp
)

target_include_directories(CheckpointActivityTest PRIVATE
  ${REPO_ROOT}/src
  ${CHECKPOINT_CORE_DIR}
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/test/fs_helpers/stubs
)

target_compile_definitions(CheckpointActivityTest PRIVATE
  CROSSPOINT_RSVP=1
  XML_GE=0
  XML_CONTEXT_BYTES=0
  CHECKPOINT_FIXTURES_DIR="${REPO_ROOT}/fixtures/checkpoints"
  CHECKPOINT_REPO_ROOT="${REPO_ROOT}"
)

# The firmware code under test builds without exceptions or RTTI, warning-free.
set_source_files_properties(${CHECKPOINT_ACTIVITY_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-fno-rtti;-Wall;-Wextra;-Wshadow;-Werror"
)

target_link_libraries(CheckpointActivityTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CheckpointActivityTest)
