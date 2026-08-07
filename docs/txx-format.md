# TXX Format Version 1

The `.txx` format stores UTF-8 text as an authenticated encrypted payload.

## Versioning Policy

Version 1 decoders accept only `version = 1` and `header_len = 96`. Future format versions must use a new version number and may change the header layout only in ways documented by that version. Older decoders deliberately reject newer versions as unsupported rather than attempting partial compatibility.

## File Layout

```text
fixed header      96 bytes
salt              16 bytes
nonce             24 bytes
authentication tag 16 bytes
ciphertext        header.ciphertext_len bytes
```

The fixed header is authenticated as AEAD associated data. The salt and nonce are public, and any tampering with them causes authentication failure because the derived key or AEAD nonce changes.

## Header

All integer fields are little-endian.

```text
offset size field
0      8    magic: 54 58 58 31 0d 0a 1a 0a
8      2    version: 1
10     2    header length: 96
12     4    flags: bit 0 means payload is compressed
16     2    crypto algorithm: 1 = XChaCha20-Poly1305 via Monocypher
18     2    KDF algorithm: 1 = Argon2id via Monocypher
20     2    compression algorithm: 0 = none, 1 = raw DEFLATE via microz
22     2    reserved algorithm field, zero
24     4    Argon2 memory blocks, each block is 1024 bytes
28     4    Argon2 passes
32     4    Argon2 lanes
36     4    salt length
40     4    nonce length
44     4    tag length
48     8    original UTF-8 plaintext length
56     8    compressed/plain payload length before encryption
64     8    ciphertext length
72     24   reserved, zero
```

For version 1, `payload_len` and `ciphertext_len` are equal because the AEAD tag is stored separately.

## Processing Order

To save a file:

1. Validate the input as UTF-8.
2. Compress the plaintext when it passes the size threshold and produces a smaller payload.
3. Generate a fresh salt and nonce.
4. Derive a 32-byte key with Argon2id.
5. Encrypt the payload with XChaCha20-Poly1305 and authenticate the fixed header as associated data.

To open a file:

1. Parse and validate the public header.
2. Derive the key from the supplied password and stored salt.
3. Verify AEAD authentication before decompression.
4. Decompress only after authentication, using the authenticated original length as the strict output limit.
5. Validate the resulting plaintext as UTF-8 before returning it.
