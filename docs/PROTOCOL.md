# Demo application-fragment protocol

This repository uses a small, generic protocol so the reassembly logic can be tested without exposing any production or hardware-specific packet format.

> This is **application-level fragmentation over UDP**, not IPv4 fragmentation.

All multibyte integers use network byte order (big-endian).

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 4 | Magic | `QUFR` (`0x51554652`) |
| 4 | 1 | Version | Protocol version, currently `1` |
| 5 | 1 | Stream ID | Independent source/channel identifier |
| 6 | 1 | Batch ID | Message sequence number, wraps `255 → 0` |
| 7 | 1 | Fragment count | Number of UDP datagrams in the message |
| 8 | 1 | Fragment index | Zero-based index inside the message |
| 9 | 1 | Flags | Reserved for application use |
| 10 | 2 | Payload length | Number of payload bytes in this datagram |
| 12 | 4 | Total message length | Expected bytes after reassembly |
| 16 | 4 | Message CRC32 | CRC32 of the complete message |
| 20 | N | Payload | Fragment bytes |

## Ordering policy

The first completed batch on each stream becomes the synchronization point. A later completed batch is accepted when its unsigned 8-bit forward distance is between 1 and 127. This makes `254 → 255 → 0 → 1` valid while preventing the delivered sequence from moving backwards.

If a newer completed batch skips one or more batch IDs, the missing IDs are reported and processing continues. A batch that completes after a newer batch has already been delivered is treated as late and dropped.
