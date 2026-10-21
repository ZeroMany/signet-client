# Unlocking Signet with a YubiKey

You can set up Signet so that unlocking needs **both** your master password
**and** a touch of your YubiKey. Without the YubiKey, even the right password
won't open the vault. Everything happens on your computer. There's no
account, no server and no internet connection.

## How it works

The Signet never sees your master password. The app scrambles it into a
32-byte *login key*, and the Signet uses that key to decrypt the vault key
it keeps inside. The vault stays encrypted on the device the whole time.

With a YubiKey switched on, the app makes the login key from two things:

1. your password, scrambled with scrypt as before, and
2. your YubiKey's answer to a *challenge*. The YubiKey only answers after
   you touch it.

The challenge is built from a random value (the *salt*) that the Signet
stores. So nothing extra is saved on your computer, and any computer with
this app can unlock your Signet as long as you have the password and the
YubiKey. A new salt, and so a new challenge, is made each time you change
your password or your key.

The Signet also records that a YubiKey is required. This app then always
asks for the key. Older Signet apps don't recognise the setting, so they
refuse to unlock instead of quietly trying the password alone.

## What you need

### Supported keys

The YubiKey feature uses the **HMAC-SHA1 challenge-response** feature of the
YubiKey's "OTP" application. It reaches the key through the computer's
smart-card service (PC/SC, over the key's CCID interface).

| Key | Works? |
|---|---|
| YubiKey 5 series (5 NFC, 5C, 5C NFC, 5 Nano, 5C Nano, 5Ci), including FIPS | Yes |
| YubiKey NEO | Yes |
| YubiKey 4 series | Not tested. It may work if its smart-card (CCID) interface is on. |
| YubiKey "Security Key" series (blue) | **No**. It only does FIDO. |
| YubiKey Bio | **No**. It only does FIDO. |
| Other brands | Not supported. |

Use the key over **USB**. NFC isn't supported.

The key's smart-card interface (CCID) must be on. It is on by default. You
can check in YubiKey Manager under *Interfaces*.

### Computers

| System | What's needed |
|---|---|
| Windows 10 / 11 | Nothing extra. The smart-card service is built in. |
| macOS 14 or newer | Nothing extra. |
| Linux | The `pcscd` service must be installed and running: `sudo apt install pcscd` and then `sudo systemctl enable --now pcscd.socket`. The app also needs the `libpcsclite1` library, which most desktops already have. |

## Setting up the YubiKey slot

A YubiKey has two slots. You need one of them set up for challenge-response,
with **Require touch** on. Slot 2 is usually free. Slot 1 often holds the
YubiKey's typing ("OTP") feature, and overwriting it removes that.

**With YubiKey Manager** (free from Yubico):

1. Open *Applications → OTP*.
2. Next to *Long Touch (Slot 2)*, click *Configure*, then choose
   *Challenge-response*.
3. Click *Generate* to make a random secret key.
4. **Write the secret down** (40 characters, 0-9 and a-f) and keep it
   somewhere safe, away from your Signet. This is your recovery secret.
5. Tick **Require touch** and click *Finish*.

**With the command line:**

    ykman otp chalresp --touch --generate 2

It prints the generated secret. Write it down.

**Making a backup YubiKey (recommended).** Set up a second YubiKey's slot
with the *same* secret. In YubiKey Manager, paste the secret instead of
clicking *Generate*. On the command line, run
`ykman otp chalresp --touch 2 <secret>`. Either key will then unlock your
Signet.

**Already use the slot for KeePassXC?** You can share it. Signet sends a
different challenge, so it gets a different answer.

## Turning it on

1. Unlock your Signet as usual.
2. Open **Device → YubiKey...**.
3. Type your master password and pick the slot.
4. Show the app how you'd recover if the key is lost:
   - type the slot's secret, **or**
   - choose *I have a backup YubiKey*. You'll be asked to swap keys
     partway through.
5. Click **Turn on** and touch the YubiKey each time it blinks. The app asks
   for **two** touches. The second one tells it how the slot reads
   challenges, so the recovery secret works later.
6. Press and hold the Signet's button to confirm.

The app checks that the recovery secret, or the backup key, gives exactly
the same answer as your YubiKey. If it doesn't, nothing is changed.

**Then make a new backup** (Device → Backup to file). See the limits below.

## Unlocking

1. Type your master password and press **Unlock**.
2. Touch the YubiKey when it blinks.
3. Press the Signet's button.

If something goes wrong, the app tells you what happened and sends nothing
to the Signet:

- **No YubiKey, or two plugged in:** plug in just one and try again.
- **Slot not set up:** set it up as described above.
- **Not touched in time:** the key waits about 15 seconds. Try again.
- **Cancel pressed:** nothing is sent.
- **The slot answered without a touch:** someone turned off *Require touch*.
  Turn it back on in YubiKey Manager, keeping the same secret.
- **Smart-card service missing:** see *Computers* above.
- **"Incorrect password, or not the right YubiKey or recovery secret":** the
  Signet can't tell which part was wrong.

## Changing the master password

**Device → Change master password** works as before. The YubiKey stays on,
and you touch it twice: once for the old password, once for the new one.
Your recovery secret and backup key keep working.

## Switching to a different YubiKey or slot

Open **Device → YubiKey...**, choose *Switch to a different YubiKey or slot*,
and confirm your current key with a touch, or with its recovery secret if
you've lost it. Then set up the new key the same way as when turning it on.
Afterwards the old key and its secret no longer work.

## Turning it off

Open **Device → YubiKey...**, choose *Turn the YubiKey off*, enter your
password, and touch the current key (or use its recovery secret). After you
confirm on the Signet, the password alone unlocks it again.

## If you lose or break your YubiKey

On the unlock screen, tick **I don't have my YubiKey: use my recovery secret
instead**, type the slot's secret, and unlock as usual. The app computes the
YubiKey's answer itself from the secret. Then open **Device → YubiKey...** to
switch to a new key or turn the requirement off.

If you have a backup YubiKey, just use it instead.

## Limits

- **Without the key, the secret and a backup key, the vault can't be
  opened.** There's no back door. That's why the app won't turn the key on
  until you've shown a way to recover.
- **Old backups stay password-only.** A backup file made *before* you turned
  the YubiKey on can still be opened with the password alone. Restoring it
  also puts the Signet back to password-only. Make a fresh backup after
  turning the key on, and keep or delete the old ones with that in mind.
- **The recovery secret is powerful.** The written-down secret plus your
  password can unlock the vault without any YubiKey. Store it as carefully
  as the key.
- **The challenge only changes when you change your password or key.**
  Someone who borrows your YubiKey and touches it could record that one
  answer. If you think that happened, change your master password. That
  makes a new challenge.
- **The touch check is a timing check.** The YubiKey can't report whether
  *Require touch* is on. The app refuses answers that come back too quickly
  for a person to have touched the key.
- **Android isn't covered.** The Android app is paused. It can't unlock a
  Signet that needs a YubiKey (it fails like a wrong password). It never
  skips the key.
- **Tested without real hardware.** The automatic tests use a pretend
  YubiKey and a pretend Signet. Try it out with a backed-up Signet first.

## For developers

- Code: `client/hwkey/` (`loginkey.*` maths, `yubikey.*` PC/SC driver,
  `loginkeybuilder.*` background work, `rekey.*` changes to the login key).
  Screens: `client/desktop/hardwarekeydialog.*`, `touchprompt.*`, and changes
  in `loginwindow.cpp` and `changemasterpassword.cpp`.
- Device settings bytes (16 bytes, stored on the Signet): byte 0 is the
  recipe (`1` = password, `2` = password + YubiKey). Bytes 1-4 are the scrypt
  settings (log2 N, r as 2 bytes, p). For recipe 2, byte 5 is the slot, byte
  6 is the padding mode (`1` = the slot ignores trailing padding, `2` = it
  reads all 64 bytes), byte 7 is the format version (`1`), and the rest are 0.
- Challenge: `HMAC-SHA256(salt, "signet-yubikey-challenge-v1")`, padded to 64
  bytes with a byte that differs from its last byte.
- Login key: `HMAC-SHA256(scrypt(password, salt), "signet-yubikey-login-v1" || response)`.
- The Signet firmware isn't changed. It already treats the login key as an
  opaque 32-byte AES key and stores the settings bytes as they are.
- Tests: `client/tests/hwkey/` (see HOW-TO-BUILD.md).
- KeePassXC's smart-card YubiKey driver was used as a reference for the
  commands and padding. No code was copied.
