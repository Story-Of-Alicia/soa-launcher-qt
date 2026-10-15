// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once
#include <QWidget>

namespace soa::runtime
{
    class Shell;
}

class WineSettings : public QWidget
{
    Q_OBJECT
public:
    explicit WineSettings(soa::runtime::Shell* shell, QWidget* parent = nullptr);

private:
    void setup_dxvk_option();
    void setup_prefix_option();
    void setup_wine_binary_option();
    void setup_tricks_option();
    void setup_wine_args_option();

    soa::runtime::Shell* shell {};
};
