add_library(soa_runtime_support STATIC
        MacWineRuntime.cpp
        WineProcess.cpp
        WineRegistry.cpp
        ${SOA_INCLUDE_DIR}/runtime/MacWineRuntime.hpp
        ${SOA_INCLUDE_DIR}/runtime/WineProcess.hpp
        ${SOA_INCLUDE_DIR}/runtime/WineRegistry.hpp
)

target_include_directories(soa_runtime_support PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_runtime_support PUBLIC
        soa_common
        Qt6::Core
)

soa_apply_build_options(soa_runtime_support)

add_library(soa_runtime STATIC
        AliciaLogHook.cpp
        GameIntegrityWatcher.cpp
        GamePhase.cpp
        GameSession.cpp
        MacLaunchDiagnostics.cpp
        PrefixInspector.cpp
        PrefixSetupJob.cpp
        ProcessRunner.cpp
        RuntimeEnvironment.cpp
        RuntimeLocator.cpp
        RuntimeTools.cpp
        Shell.cpp
        SystemProfile.cpp

        ${SOA_INCLUDE_DIR}/runtime/AliciaLogHook.hpp
        ${SOA_INCLUDE_DIR}/runtime/GameIntegrityWatcher.hpp
        ${SOA_INCLUDE_DIR}/runtime/GameSession.hpp
        ${SOA_INCLUDE_DIR}/runtime/MacLaunchDiagnostics.hpp
        ${SOA_INCLUDE_DIR}/runtime/PrefixInspector.hpp
        ${SOA_INCLUDE_DIR}/runtime/PrefixSetupJob.hpp
        ${SOA_INCLUDE_DIR}/runtime/ProcessRunner.hpp
        ${SOA_INCLUDE_DIR}/runtime/RuntimeLocator.hpp
        ${SOA_INCLUDE_DIR}/runtime/Shell.hpp
        ${SOA_INCLUDE_DIR}/runtime/SystemProfile.hpp
)

target_include_directories(soa_runtime
        PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_runtime
        PUBLIC
        soa_common
        soa_config
        soa_runtime_support
        soa_i18n
        soa_network
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        PRIVATE
        Qt6::Concurrent
)

soa_apply_build_options(soa_runtime)
