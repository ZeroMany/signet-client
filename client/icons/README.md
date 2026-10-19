# Entry icons

The app shows an icon next to each entry using the
[aegis-simple-icons](https://github.com/alexbakker/aegis-simple-icons) pack:
brand icons from [Simple Icons](https://simpleicons.org), drawn as white logos
on circles in each brand's color.

The icons are compiled into the app, so it never downloads anything. You
can also import a different pack (see below).

## How an entry gets its icon

`client/desktop/entryiconprovider.cpp` tries, in order:

1. The entry's whole name, ignoring case, spaces and punctuation
   ("GitHub", "dev.to").
2. The entry's web address. For `https://accounts.google.com` it tries
   "google" and skips generic parts like "accounts" or "www".
3. The first word of the entry's name ("Google work account").

If nothing matches, the app shows its usual generic icon.

## Using your own icon pack

Open **File → Settings** and click **Import icon pack...**, then pick a
`.zip` file in the Aegis icon pack format (a `pack.json` plus the icon
files it lists, SVG, PNG or JPEG). This works the same way as the
Aegis and Yubico Authenticator apps, so any pack made for them works here.

The imported pack is copied to the `signet/icon-pack` folder next to the
app's settings file and is used instead of the built-in pack. Click
**Use built-in icons** to delete it and go back.

The app only copies files that `pack.json` lists, checks that their names
stay inside that folder, and refuses packs over 100 MB.

## Updating the built-in pack

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
