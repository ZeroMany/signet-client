#ifndef SAFEACTIONS_H
#define SAFEACTIONS_H

#include <QString>

class QWidget;

//
// Small helpers for the two ways data leaves the app at the user's
// request: copying to the clipboard and opening a website.
//
namespace SafeActions {

// How long a copied value stays on the clipboard.
const int clipboardClearSeconds = 30;

// Copies text to the clipboard, asks clipboard managers and Windows
// clipboard history / cloud clipboard not to keep it, and clears it
// again after clipboardClearSeconds unless something else was copied
// since.
void copySecret(const QString &text);

// Clears the clipboard if it still holds something this app copied.
// Called on logout and when the app quits.
void clearClipboard();

// Opens a web address in the browser. Only http and https addresses are
// opened (an address with no scheme gets https); anything else, such as
// file: links, is refused with a message, since it could run a program
// or connect to another computer.
bool openWebsite(const QString &address, QWidget *parent);

}

#endif // SAFEACTIONS_H
