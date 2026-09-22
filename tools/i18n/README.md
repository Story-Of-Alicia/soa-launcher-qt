# Translations

Translations are grouped into contexts so they are easier to work with in Qt Linguist:

- Launcher & Navigation
- Home & Account
- Setup & Rules
- Game Installation & Repair
- Game Launch & Session
- Runtime & Compatibility
- Network & Transfers
- Launcher Updates
- Settings
- About & Credits
- Logs & Diagnostics

Qt Linguist may show these alphabetically.

The launcher checks these contexts when looking up translations. Each source
string should only exist in one category.

Some strings are created at runtime and cannot be picked up properly by
`lupdate`. Those are added manually in `src/i18n/TranslationCatalog.cpp`.

Useful commands:

    tools/i18n/filter-translatable.py --check translations/*.ts
    tools/i18n/filter-translatable.py --missing translations/*.ts
    tools/i18n/filter-translatable.py --list translations/soa_launcher_en.ts
    tools/i18n/filter-translatable.py --prune translations/*.ts

`--missing` checks strings used through `soa::i18n::translate(...)` along with
the runtime strings registered in `TranslationCatalog.cpp`. It also checks that
English, Norwegian and Dutch contain the same source strings.

`--prune` removes things that should not be translated, such as command-line
flags, environment variables, registry data, paths and filenames. Names like
Wine, Proton and Discord also do not need their own translation, but sentences
containing them still do.

English is the source language, so it is not built into a `.qm` file. Keep the
English translations empty and marked as unfinished.

Norwegian and Dutch entries also start empty and unfinished. When a translation
is finished, remove `type="unfinished"` from that entry.

## Markup

Try to keep markup out of translated strings. Build the layout in code and only
translate the actual text.

Small things like `<b>` and `<br>` are fine when they are part of the sentence.