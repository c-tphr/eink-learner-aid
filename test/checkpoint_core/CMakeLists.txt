set(CHECKPOINT_CORE_DIR ${REPO_ROOT}/lib/CheckpointCore)
set(CHECKPOINT_CORE_SOURCES
  ${CHECKPOINT_CORE_DIR}/CheckpointIndex.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointParser.cpp
  ${CHECKPOINT_CORE_DIR}/CheckpointSource.cpp
  ${CHECKPOINT_CORE_DIR}/JsonText.cpp
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
  ${CHECKPOINT_CORE_SOURCES}
)
add_dependencies(CheckpointCoreTest CheckpointCoreNoRttiCheck)

target_include_directories(CheckpointCoreTest PRIVATE
  ${CHECKPOINT_CORE_DIR}
)

target_compile_definitions(CheckpointCoreTest PRIVATE
  CHECKPOINT_FIXTURES_DIR="${REPO_ROOT}/fixtures/checkpoints"
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
