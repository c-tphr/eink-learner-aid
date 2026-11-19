# Host test of the per-book state file service (src/reading/BookStateFile, task C5) on an SD card in
# memory with a power cut after any number of changes (stubs/HalStorage.h): the atomic write through a
# temporary file and a rename, and the read of a complete temporary file a cut left before its rename.

set(CHECKPOINT_CORE_DIR ${REPO_ROOT}/lib/CheckpointCore)
set(BOOK_STATE_FILE_SOURCES
  ${REPO_ROOT}/src/reading/BookStateFile.cpp
  ${REPO_ROOT}/src/reading/BookStateRules.cpp
)

add_executable(BookStateFileTest
  BookStateFileTest.cpp
  ${BOOK_STATE_FILE_SOURCES}
  ${CHECKPOINT_CORE_DIR}/BookState.cpp
  ${CHECKPOINT_CORE_DIR}/GradeTable.cpp
  ${CHECKPOINT_CORE_DIR}/StateCodec.cpp
  ${CHECKPOINT_CORE_DIR}/StateRestore.cpp
  ${CHECKPOINT_CORE_DIR}/JsonText.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointTypes.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointIndex.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointParser.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointSource.cpp
)

target_include_directories(BookStateFileTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${CHECKPOINT_CORE_DIR}
)

target_compile_definitions(BookStateFileTest PRIVATE CROSSPOINT_RSVP=1)

# The firmware code under test builds without exceptions or RTTI, warning-free.
set_source_files_properties(${BOOK_STATE_FILE_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-fno-rtti;-Wall;-Wextra;-Wshadow;-Werror"
)

target_link_libraries(BookStateFileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookStateFileTest)
