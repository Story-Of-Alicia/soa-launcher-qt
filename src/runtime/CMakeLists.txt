add_library(soa_runtime STATIC
        AliciaLogHook.cpp
        GameIntegrityWatcher.cpp
        GamePhase.cpp
        GameSession.cpp
        MacLaunchDiagnostics.cpp
        MacWineRuntime.cpp
        PrefixInspector.cpp
        PrefixSetupJob.cpp
        ProcessRunner.cpp
        RuntimeEnvironment.cpp
        RuntimeLocator.cpp
        Shell.cpp
        SystemProfile.cpp
        WineProcess.cpp
        WineRegistry.cpp

        ${SOA_INCLUDE_DIR}/runtime/AliciaLogHook.hpp
        ${SOA_INCLUDE_DIR}/runtime/GameIntegrityWatcher.hpp
        ${SOA_INCLUDE_DIR}/runtime/GameSession.hpp
        ${SOA_INCLUDE_DIR}/runtime/MacLaunchDiagnostics.hpp
        ${SOA_INCLUDE_DIR}/runtime/MacWineRuntime.hpp
        ${SOA_INCLUDE_DIR}/runtime/PrefixInspector.hpp
        ${SOA_INCLUDE_DIR}/runtime/PrefixSetupJob.hpp
        ${SOA_INCLUDE_DIR}/runtime/ProcessRunner.hpp
        ${SOA_INCLUDE_DIR}/runtime/RuntimeLocator.hpp
        ${SOA_INCLUDE_DIR}/runtime/Shell.hpp
        ${SOA_INCLUDE_DIR}/runtime/SystemProfile.hpp
        ${SOA_INCLUDE_DIR}/runtime/WineProcess.hpp
        ${SOA_INCLUDE_DIR}/runtime/WineRegistry.hpp
)

target_include_directories(soa_runtime
        PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_runtime
        PUBLIC
        soa_common
        soa_network
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)
