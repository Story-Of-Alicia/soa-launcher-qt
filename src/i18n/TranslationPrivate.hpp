#pragma once

#include <QString>

namespace util::i18n::detail
{
    void set_catalog_active(bool active);
    [[nodiscard]] QString translated(const QString& source);
}
