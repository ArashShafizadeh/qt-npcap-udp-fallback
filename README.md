# Qt Npcap UDP Fallback

A small Qt 6 reference application that combines the normal `QUdpSocket` receive path with an optional Npcap raw-frame path on Windows.

The raw-frame path is useful when a device sends an otherwise valid IPv4/UDP packet with a stale or incorrect destination MAC address. In that situation, Windows may discard the Ethernet frame before it reaches `QUdpSocket`, while Npcap in promiscuous mode can still expose the frame to the application.

> This repository is a generic demonstration. It contains no hardware-specific protocol, packet IDs, production addresses, calibration data, or private application logic.

## What it demonstrates

- Normal UDP reception through `QUdpSocket`
- Runtime loading of `wpcap.dll` with no Npcap SDK build dependency
- Automatic adapter discovery using the selected local IPv4 address
- BPF filtering by destination IPv4 address and UDP port range
- Ethernet, stacked VLAN/QinQ, raw IPv4, and `DLT_NULL` parsing
- Cross-path duplicate suppression when both Qt and Npcap see the same datagram
- Graceful standard-UDP-only operation when Npcap is unavailable
- A simple Qt Widgets dashboard with counters and a packet log
- Unit tests for the raw IPv4/UDP parser

## Architecture

```text
                           +----------------------+
Device --> Ethernet/NIC -->| Windows network stack|--> QUdpSocket ----+
          |                +----------------------+                  |
          |                                                           v
          +--> Npcap promiscuous capture --> frame parser --> deduplicator --> application
```

Npcap runs in parallel rather than waiting for a failure. This matters because a frame rejected at layer 2 may never produce a socket-level error that the application can detect.

## Requirements

- Qt 6.4 or newer with the Widgets and Network modules
- CMake 3.21 or newer
- A C++17 compiler
- Windows for the Npcap path
- Npcap installed on the machine; Wireshark installations commonly include it

The project also builds on Linux and macOS, but there it operates in `QUdpSocket`-only mode.

## Build with Qt Creator

1. Open `CMakeLists.txt` in Qt Creator.
2. Select a Qt 6 desktop kit.
3. Configure and build.
4. Run `qt-npcap-udp-fallback`.

## Build from a terminal

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Windows, run the produced executable from the Release output directory. Npcap must be installed on the target computer; `wpcap.dll` is loaded at runtime.

## Usage

1. Select the local IPv4 address of the adapter connected to the device. `0.0.0.0` scans all non-loopback IPv4 adapters.
2. Enter one destination UDP port or a port range.
3. Keep **Enable Npcap raw-frame fallback** selected.
4. Start the receiver.
5. Use **Send to first port** for a basic `QUdpSocket` smoke test.
6. Connect the real device to test the raw-frame fallback scenario.

## Important limitations

- The parser currently accepts IPv4/UDP only.
- Fragmented IPv4 datagrams are intentionally rejected.
- Npcap can expose frames rejected by the OS only when the NIC/driver actually provides them in promiscuous mode.
- Some systems require elevated privileges or an Npcap installation configured for non-admin capture.
- The demo opens one `QUdpSocket` per port and limits the range to 256 ports.
- The duplicate window is intentionally short and only suppresses the same packet observed through the opposite receive path.

## Why not ship `wpcap.dll` in the repository?

Npcap has its own distribution and licensing terms. This project dynamically loads an existing local installation and does not redistribute Npcap binaries.

## Repository topics

Suggested GitHub topics:

`qt` `qt6` `cpp` `udp` `npcap` `pcap` `wireshark` `networking` `packet-capture` `windows`

## License

The source code in this repository is available under the MIT License. Npcap itself is a separate third-party product and is not covered by this repository's license.
