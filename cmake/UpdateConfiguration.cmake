# Launcher update endpoints, signing key, and remotely hosted launcher documents.

if(APPLE)
    set(SOA_LAUNCHER_UPDATE_MANIFEST_URL
            "https://r2.storyofalicia.com/launcher/macos/manifest.json"
            CACHE STRING "HTTPS URL of the launcher update manifest")
    set(SOA_LAUNCHER_UPDATE_FALLBACK_MANIFEST_URL
            "https://raw.githubusercontent.com/Story-Of-Alicia/soa-launcher-qt/launcher-updates/macos/manifest.json"
            CACHE STRING "Fallback HTTPS URL of the launcher update manifest")
else()
    set(SOA_LAUNCHER_UPDATE_MANIFEST_URL
            "https://r2.storyofalicia.com/launcher/linux/manifest.json"
            CACHE STRING "HTTPS URL of the launcher update manifest")
    set(SOA_LAUNCHER_UPDATE_FALLBACK_MANIFEST_URL
            "https://raw.githubusercontent.com/Story-Of-Alicia/soa-launcher-qt/launcher-updates/linux/manifest.json"
            CACHE STRING "Fallback HTTPS URL of the launcher update manifest")
endif()

file(READ
        "${SOA_ROOT_DIR}/packaging/soa-update-public-key.hex"
        SOA_DEFAULT_UPDATE_PUBLIC_KEY_HEX)
string(STRIP
        "${SOA_DEFAULT_UPDATE_PUBLIC_KEY_HEX}"
        SOA_DEFAULT_UPDATE_PUBLIC_KEY_HEX)
set(SOA_LAUNCHER_UPDATE_PUBLIC_KEY_HEX
        "${SOA_DEFAULT_UPDATE_PUBLIC_KEY_HEX}"
        CACHE STRING "Raw Ed25519 public key used by SOA Seal v1 as 64 hexadecimal characters")

string(LENGTH
        "${SOA_LAUNCHER_UPDATE_PUBLIC_KEY_HEX}"
        SOA_LAUNCHER_UPDATE_PUBLIC_KEY_HEX_LENGTH)

if(SOA_LAUNCHER_UPDATE_PUBLIC_KEY_HEX)
    if(NOT SOA_LAUNCHER_UPDATE_PUBLIC_KEY_HEX_LENGTH EQUAL 64
            OR NOT SOA_LAUNCHER_UPDATE_PUBLIC_KEY_HEX MATCHES "^[0-9A-Fa-f]+$")
        message(FATAL_ERROR
                "SOA_LAUNCHER_UPDATE_PUBLIC_KEY_HEX must be exactly 64 hexadecimal characters")
    endif()
endif()

set(SOA_RULES_DOCUMENT_URL
        "https://docs.google.com/document/d/1vry3ZuDtzdS_mX1P2udWlb8z2Q9Atr3p1THZdtZ2EHA/export?format=html"
        CACHE STRING "HTTPS URL of the authoritative rules document")
