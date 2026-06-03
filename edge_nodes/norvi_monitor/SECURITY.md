# Hardware Security Playbook (Silicon Shield)

This document contains the physical commands required to lock the ESP32 silicon before deploying the Norvi Monitor to the production environment.

> [!WARNING]
> Burning eFuses is an **IRREVERSIBLE** physical operation. Once burnt, the hardware cannot be downgraded, debugged via JTAG, or flashed with an unencrypted/unsigned binary. Proceed only on production-ready silicon.

## 1. Secure Boot V2 Configuration

Secure Boot V2 ensures that only firmware signed with your RSA-3072 private key can boot on the device.

**Generate a Private Signing Key:**
```bash
espsecure.py generate_signing_key --version 2 secure_boot_signing_key.pem
```
*(Store this key in a secure offline vault. Never commit it to the repository.)*

**Burn the Public Key Digest & Enable Secure Boot:**
```bash
espefuse.py --port COMx burn_key secure_boot secure_boot_signing_key.pem
espefuse.py --port COMx burn_efuse SECURE_BOOT_EN
```

## 2. Flash Encryption (AES-256)

Flash Encryption ensures the firmware stored in the external flash is encrypted, protecting the `secrets.h` contents, certificates, and IP. We will use the ESP32's hardware RNG to generate an unknown key that never leaves the chip.

**Enable Flash Encryption (Release Mode):**
```bash
espefuse.py --port COMx burn_efuse FLASH_CRYPT_CNT
espefuse.py --port COMx burn_efuse FLASH_CRYPT_CONFIG 0xF
```
*(When the device boots for the first time, the bootloader will encrypt the plaintext flash and burn the encryption key using the hardware RNG.)*

## 3. Disabling JTAG & UART Download Modes

To prevent physical extraction or debugging attacks on the silicon, permanently disable the JTAG interface and the UART ROM download mode.

**Disable JTAG:**
```bash
espefuse.py --port COMx burn_efuse DISABLE_JTAG
```

**Disable ROM Download Mode (UART):**
```bash
espefuse.py --port COMx burn_efuse DISABLE_DL_MODE
```
