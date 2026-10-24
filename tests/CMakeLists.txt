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
    "${REPO}/src/Actions/Action.cpp"
    "${REPO}/src/Actions/ActionData.cpp"
    "${REPO}/src/Actions/ActionRegistry.cpp"
    "${REPO}/src/Actions/ActionWindow.cpp"
    "${REPO}/src/Actions/PersistentActionSet.cpp"
    "${REPO}/src/Actions/Json/JsonSchema.cpp"
    "${REPO}/src/Combat/CombatLogic.cpp"
    "${REPO}/src/Context/ContextGate.cpp"
    "${REPO}/src/Inventory/ItemQuery.cpp"
    "${REPO}/src/Movement/StanceLogic.cpp"
    "${REPO}/src/Movement/AimLogic.cpp"
    "${REPO}/src/Menus/PopupPolicy.cpp"
    "${REPO}/src/Menus/SpeakerMatch.cpp"
    "${REPO}/src/Menus/SubtitleTracker.cpp"
    "${REPO}/src/utils/ScopedState.cpp"
    "${REPO}/src/utils/PulseQueue.cpp"
)

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
