# LanguageManager still performs widget-tree retranslations and toggles translated
# UI assets. Keep it in soa_ui for now, while giving i18n its correct filesystem
# ownership. It can become an independent target when UI-specific behavior is split.
target_sources(soa_ui PRIVATE
        LanguageManager.cpp
        ${SOA_INCLUDE_DIR}/i18n/LanguageManager.hpp
)
