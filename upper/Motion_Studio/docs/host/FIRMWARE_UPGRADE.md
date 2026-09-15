# Firmware Upgrade Status

The audited firmware snapshot does not contain a product bootloader or host upgrade protocol. AT32 Motion Studio therefore does **not** implement erase/program/verify packets.

Development flashing still exists outside the product Host via repository tooling such as J-Link/DAP helpers, but that is not equivalent to a field-update protocol.

Before enabling the Firmware workspace, a future firmware revision must define and implement at least:

- supported transport(s),
- bootloader entry condition/command,
- image format and valid application address,
- erase/program unit,
- packet framing,
- integrity check (CRC/hash),
- ACK/NACK and error codes,
- timeout/retry behavior,
- verify/reboot/reconnect behavior,
- version/compatibility rules.

Until those exist in firmware, the UI remains an explicit unsupported state.
