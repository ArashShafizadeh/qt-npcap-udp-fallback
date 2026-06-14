# LinkedIn post

I have published a new open-source C++/Qt project: **Qt Npcap UDP Fallback**.

The project demonstrates a hybrid UDP receiver for Windows. It uses `QUdpSocket` for normal UDP reception and Npcap as a raw Ethernet fallback when Windows drops a frame before it reaches the socket layer, such as when a device sends a packet with a stale destination MAC address.

Key features include runtime Npcap loading, IPv4/UDP parsing, VLAN and QinQ support, adapter discovery, BPF filtering, duplicate suppression, a Qt Widgets monitoring interface, and parser tests.

Repository:
https://github.com/ArashShafizadeh/qt-npcap-udp-fallback

#cpp #qt #qt6 #udp #npcap #networking #opensource #windows
