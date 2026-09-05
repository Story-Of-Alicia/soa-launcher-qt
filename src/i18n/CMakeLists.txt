add_library(soa_i18n STATIC
        LanguageManager.cpp
        TranslationCatalog.cpp
        WidgetTranslator.cpp
        TranslationPrivate.hpp
        ${SOA_INCLUDE_DIR}/i18n/LanguageManager.hpp
)

target_include_directories(soa_i18n
        PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_i18n
        PUBLIC
        soa_common
        soa_config
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

soa_apply_build_options(soa_i18n)
