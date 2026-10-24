cmake_minimum_required(VERSION 3.20)
# Host-side tests for the engine-independent logic. This is a separate project from the 32-bit Windows plugin
# (the root CMakeLists rejects host builds): it compiles selected plugin sources against a fake NeuroSDK transport
# and never links game, xNVSE or libneurosdk code. It cannot prove any in-game behaviour.
project(neuro_fnv_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(REPO "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(neuro_fnv_tests
    main.cpp
    FakeNeuro.cpp
    ActionRegistryTests.cpp
    ActionWindowTests.cpp
    ActionDataTests.cpp
    JsonSchemaTests.cpp
    InventoryTests.cpp
    ScopedStateTests.cpp
    PopupPolicyTests.cpp
    CombatTests.cpp
    StanceTests.cpp
    InputCoreTests.cpp
    ContextGateTests.cpp
    SpeakerMatchTests.cpp
    AimLogicTests.cpp
    WalkerLogicTests.cpp
    DistanceTests.cpp
    IdlePokerTests.cpp
    DamageTests.cpp
    LocationTests.cpp
    ObjectPriorityTests.cpp
    SeePushTests.cpp
    IdListTests.cpp
    "${REPO}/src/Actions/Action.cpp"
    "${REPO}/src/Actions/ActionData.cpp"
    "${REPO}/src/Actions/ActionRegistry.cpp"
    "${REPO}/src/Actions/ActionWindow.cpp"
    "${REPO}/src/Actions/IdList.cpp"
    "${REPO}/src/Actions/PersistentActionSet.cpp"
    "${REPO}/src/Actions/Json/JsonSchema.cpp"
    "${REPO}/src/Combat/CombatLogic.cpp"
    "${REPO}/src/Combat/DamageLogic.cpp"
    "${REPO}/src/Context/ContextGate.cpp"
    "${REPO}/src/Context/IdlePoker.cpp"
    "${REPO}/src/Context/Greeting.cpp"
    "${REPO}/src/Context/Location.cpp"
    "${REPO}/src/Context/ObjectPriority.cpp"
    "${REPO}/src/Context/SeePush.cpp"
    "${REPO}/src/Inventory/ItemQuery.cpp"
    "${REPO}/src/Movement/StanceLogic.cpp"
    "${REPO}/src/Movement/AimLogic.cpp"
    "${REPO}/src/Movement/WalkerLogic.cpp"
    "${REPO}/src/Context/Distance.cpp"
    "${REPO}/src/Menus/PopupPolicy.cpp"
    "${REPO}/src/Menus/SpeakerMatch.cpp"
    "${REPO}/src/Menus/SubtitleTracker.cpp"
    "${REPO}/src/utils/ScopedState.cpp"
    "${REPO}/src/utils/PulseQueue.cpp"
)

# Forgetting to list something must not go unnoticed. A test file that is not compiled is an error (it would silently
# never run); an engine-free source (the directories below hold only logic that builds on the host) that is not
# compiled here is a warning, which tools/test.sh prints.
get_target_property(listed_sources neuro_fnv_tests SOURCES)
file(GLOB found_test_files "${CMAKE_CURRENT_SOURCE_DIR}/*Tests.cpp")
foreach(test_file IN LISTS found_test_files)
    list(FIND listed_sources "${test_file}" listed_index)
    get_filename_component(test_name "${test_file}" NAME)
    list(FIND listed_sources "${test_name}" listed_by_name)
    if(listed_index EQUAL -1 AND listed_by_name EQUAL -1)
        message(FATAL_ERROR "tests/${test_name} is not listed in tests/CMakeLists.txt, so it would never run.")
    endif()
endforeach()
file(GLOB engine_free_sources
    "${REPO}/src/Combat/*.cpp" "${REPO}/src/Context/*.cpp" "${REPO}/src/Movement/*.cpp")
foreach(source_file IN LISTS engine_free_sources)
    list(FIND listed_sources "${source_file}" listed_index)
    if(listed_index EQUAL -1)
        get_filename_component(source_name "${source_file}" NAME)
        message(WARNING "${source_name} is in an engine-free directory but not in the host tests (tests/CMakeLists.txt).")
    endif()
endforeach()

# The stubs directory must come first: it shadows src/NeuroSDK.hpp and the game's Utils/DebugLog.hpp.
target_include_directories(neuro_fnv_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${REPO}/src"
    "${REPO}/libs/libneurosdk/vendor"
)
target_compile_options(neuro_fnv_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)

enable_testing()
add_test(NAME neuro_fnv_tests COMMAND neuro_fnv_tests)
