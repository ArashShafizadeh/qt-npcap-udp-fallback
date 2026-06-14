Initial public release of **Qt UDP Frame Reassembler**, a reusable Qt 5/6 and C++17 component for reconstructing application-level messages split across UDP datagrams.

## Features

- Out-of-order fragment reassembly
- Multiple interleaved batches
- Duplicate-fragment suppression
- Conflicting-fragment detection
- Incomplete-assembly timeouts
- Active-assembly and message-size limits
- CRC32 and total-length validation
- Missing batch detection
- Late-batch suppression
- Unsigned 8-bit batch wrap-around
- Independent stream IDs
- Qt Widgets UDP scenario simulator
- Qt Test coverage for core edge cases

## Notes

- The included wire format is a generic demonstration protocol.
- No hardware-specific or production packet format is included.
- Fragmentation is performed at the application layer, not through IPv4 fragmentation.
