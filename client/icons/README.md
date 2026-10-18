# Entry icons

The app shows an icon next to each entry using the
[aegis-simple-icons](https://github.com/alexbakker/aegis-simple-icons) pack:
brand icons from [Simple Icons](https://simpleicons.org), drawn as white logos
on circles in each brand's color.

The icons are compiled into the app, so it never downloads anything.

## How an entry gets its icon

`client/desktop/entryiconprovider.cpp` tries, in order:

1. The entry's whole name, ignoring case, spaces and punctuation
   ("GitHub", "dev.to").
2. The entry's web address. For `https://accounts.google.com` it tries
   "google" and skips generic parts like "accounts" or "www".
3. The first word of the entry's name ("Google work account").

If nothing matches, the app shows its usual generic icon.

## Updating the pack

1. Download the newest `aegis-simple-icons-vNNN.zip` from the
   [releases page](https://github.com/alexbakker/aegis-simple-icons/releases).
2. From the repository's top folder, run
   `python3 client/icons/update-icons.py path/to/aegis-simple-icons-vNNN.zip`
3. Rebuild the app.

The current pack is version 268, built from Simple Icons as of 11 Oct 2026.

## License

The icons are released under CC0 1.0 (public domain) by the Simple Icons
project. The logos remain trademarks of their owners; see the Simple Icons
[disclaimer](https://github.com/simple-icons/simple-icons/blob/develop/DISCLAIMER.md).
