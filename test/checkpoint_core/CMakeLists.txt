set(CHECKPOINT_CORE_DIR ${REPO_ROOT}/lib/CheckpointCore)
set(CHECKPOINT_CORE_SOURCES
  ${CHECKPOINT_CORE_DIR}/CheckpointIndex.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointParser.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointSource.cpp
  ${CHECKPOINT_CORE_DIR}/JsonText.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointTypes.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointTrigger.cpp
  ${CHECKPOINT_CORE_DIR}/GradeTable.cpp
  ${CHECKPOINT_CORE_DIR}/BookState.cpp
  ${CHECKPOINT_CORE_DIR}/StateCodec.cpp
  ${CHECKPOINT_CORE_DIR}/StateRestore.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointFlow.cpp
)

# Compile-only check that the library needs neither exceptions nor RTTI, as on
# the device. The test binary links its own copy built with RTTI, which UBSan's
# vptr check needs.
add_library(CheckpointCoreNoRttiCheck OBJECT ${CHECKPOINT_CORE_SOURCES})
target_compile_options(CheckpointCoreNoRttiCheck PRIVATE
  -fno-exceptions -fno-rtti -Wall -Wextra -pedantic -Wconversion -Wshadow -Werror)

add_executable(CheckpointCoreTest
  CheckpointManifestTest.cpp
  CheckpointIndexTest.cpp
  CheckpointRobustnessTest.cpp
  CheckpointTriggerTest.cpp
  CheckpointFlowTest.cpp
  GradeRulesTest.cpp
  StateCodecTest.cpp
  StateRestoreTest.cpp
  CheckpointScenarioTest.cpp
  ${CHECKPOINT_CORE_SOURCES}
)
add_dependencies(CheckpointCoreTest CheckpointCoreNoRttiCheck)

target_include_directories(CheckpointCoreTest PRIVATE
  ${CHECKPOINT_CORE_DIR}
)

target_compile_definitions(CheckpointCoreTest PRIVATE
  CHECKPOINT_FIXTURES_DIR="${REPO_ROOT}/fixtures/checkpoints"
  CHECKPOINT_BOOKS_DIR="${REPO_ROOT}/fixtures/books"
  CHECKPOINT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  CHECKPOINT_PLAN_PATH="${REPO_ROOT}/docs/PLAN.md"
)

# AddressSanitizer and UndefinedBehaviorSanitizer for this target only. Any
# report aborts the test. Set CHECKPOINT_CORE_SANITIZE=OFF to build without.
option(CHECKPOINT_CORE_SANITIZE "Build CheckpointCoreTest with ASan and UBSan" ON)
if(CHECKPOINT_CORE_SANITIZE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(CheckpointCoreTest PRIVATE
    -fsanitize=address,undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
  )
  target_link_options(CheckpointCoreTest PRIVATE -fsanitize=address,undefined)
endif()

target_link_libraries(CheckpointCoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CheckpointCoreTest)
