if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(soa_seal_crypto STATIC
            SOASeal.c
    )

    target_include_directories(soa_seal_crypto PRIVATE
            ${SOA_INCLUDE_DIR}
    )

    target_link_libraries(soa_seal_crypto PUBLIC
            OpenSSL::Crypto
    )

    set_target_properties(soa_seal_crypto PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            AUTOMOC OFF
            AUTORCC OFF
            AUTOUIC OFF
    )

    soa_apply_build_options(soa_seal_crypto)
endif()

qt6_wrap_cpp(SOA_NETWORK_MOC_SOURCES
        ${SOA_INCLUDE_DIR}/network/CourierBridge.hpp
)

add_library(soa_network SHARED
        Courier.swift
        CourierUpdate.swift
        CourierDownload.swift
        CourierIntegrity.swift
        CourierPaths.swift
        CourierTransfer.swift
        CourierTypes.swift
        CourierABI.swift
        CourierPing.swift
        NetworkFoundation.swift
        SwiftHttpClient.swift
        LauncherUpdateService.swift
        DiscordRpcService.swift
        SwiftNetworkABI.swift
        MD5.swift
        Courier.cpp
        SwiftHttpClient.cpp
        DiscordRpc.cpp
        ${SOA_NETWORK_MOC_SOURCES}

        ${SOA_INCLUDE_DIR}/network/Courier.h
        ${SOA_INCLUDE_DIR}/network/CourierBridge.hpp
        ${SOA_INCLUDE_DIR}/network/DiscordRpc.hpp
        ${SOA_INCLUDE_DIR}/network/DownloadStatus.hpp
        ${SOA_INCLUDE_DIR}/network/SwiftHttpClient.hpp
        ${SOA_INCLUDE_DIR}/network/SwiftNetwork.h
)

target_compile_options(soa_network PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-Xcc -fmodule-map-file=${CMAKE_CURRENT_SOURCE_DIR}/module.modulemap>"
)

target_include_directories(soa_network
        PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_network PRIVATE
        soa_common
        spdlog::spdlog
        Qt6::Core
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(soa_network PRIVATE
            soa_seal_crypto
    )
endif()

set_target_properties(soa_network PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
        Swift_MODULE_NAME SoaCourier
)

soa_apply_build_options(soa_network)
