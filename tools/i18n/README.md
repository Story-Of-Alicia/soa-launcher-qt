# Translation tools

If you want to translate the launcher, you don't need anything in here. Translations are done on Weblate, read [Translating the Launcher](https://github.com/Story-Of-Alicia/soa-launcher-qt/wiki/Translating-the-Launcher) on the wiki.

This folder is for keeping the `.ts` files in `translations/` correct.

## filter-translatable.py

    tools/i18n/filter-translatable.py --check translations/*.ts
    tools/i18n/filter-translatable.py --missing translations/*.ts
    tools/i18n/filter-translatable.py --list translations/soa_launcher_en.ts
    tools/i18n/filter-translatable.py --prune translations/*.ts

- `--check` fails if a `.ts` file has stuff that shouldn't be translated, like command-line flags, environment variables, registry data, paths and filenames.
- `--missing` fails if a string used with `soa::i18n::translate(...)` or added in `src/i18n/TranslationCatalog.cpp` is missing from the `.ts` files. It also checks that all `.ts` files have the same strings in the same contexts.
- `--list` shows what `--prune` would remove and why.
- `--prune` removes it from every file you give it, so the files stay the same.

The tool won't run if the files don't have the same strings. Run `--check` and `--missing` before merging translation pull requests from Weblate.

## Contexts

Strings are grouped into contexts, like Settings or Launcher Updates. The launcher looks up translations in these contexts, so a string should only be in one of them.

## Adding new strings

`lupdate` can't keep these files updated since most strings are looked up at runtime. So new strings have to be added to every `.ts` file by hand, in the right context, with an empty translation marked `type="unfinished"`. Run `--missing` after to check you got all of them.

Strings that are built at runtime can't be found by the tool, add those in `src/i18n/TranslationCatalog.cpp`.

Please keep markup out of strings. Do the layout in code and only translate the text.