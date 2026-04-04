# ETChaCha20Poly1305 (RFC8439 AEAD)

Arduino IDE library implementing **ChaCha20-Poly1305 AEAD** per **RFC 8439**:
- 32-byte key
- 12-byte nonce (96-bit)
- AAD support
- 16-byte authentication tag

Designed for ET-Bus (UDP) style packets.

## Install
1. Download the ZIP.
2. Arduino IDE -> Sketch -> Include Library -> Add .ZIP Library...
3. Select the downloaded ZIP.

## Usage
See `File -> Examples -> ETChaCha20Poly1305 -> ETBus_AEAD_Demo`.

## Nonce requirements (IMPORTANT)
Nonce must be **unique per key**. A common ET-Bus pattern:
`nonce = device_id(4) | boot_id(4) | counter(4)` (little-endian fields).

RFC: https://www.rfc-editor.org/rfc/rfc8439
