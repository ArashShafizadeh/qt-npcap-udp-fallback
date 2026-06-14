# v1.0.0 – Initial Public Release

Initial public release of **Qt Npcap UDP Fallback**, a generic Qt 6 and C++ reference application for reliable UDP reception on Windows.

## Features

- `QUdpSocket` UDP reception
- Npcap raw Ethernet fallback
- Runtime loading of `wpcap.dll`
- IPv4 and UDP packet parsing
- Ethernet, VLAN, and QinQ support
- BPF filtering by destination address and UDP port range
- Automatic network-adapter discovery
- Cross-path duplicate suppression
- Qt Widgets monitoring interface
- Packet counters and live logging
- Unit tests for the packet parser

## Notes

- Npcap is loaded from an existing local installation and is not redistributed.
- The raw capture path is available on Windows.
- Linux and macOS builds operate in `QUdpSocket`-only mode.
- This public version contains no hardware-specific protocol or private production logic.
