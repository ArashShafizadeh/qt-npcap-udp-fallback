# GitHub setup and publishing checklist

## Repository name

```text
qt-npcap-udp-fallback
```

## Description

```text
Qt 6 and C++ UDP receiver for Windows with an Npcap raw Ethernet fallback when QUdpSocket misses frames.
```

## Topics

Paste these topics as separate entries:

```text
qt qt6 cpp udp npcap pcap qudpsocket packet-capture networking ethernet windows cmake ipv4 vlan data-acquisition
```

## Social preview

Upload:

```text
docs/assets/social-preview.png
```

from:

```text
Settings → General → Social preview → Edit
```

The image is 1280 × 640 pixels and is ready for GitHub.

## First release

Create a release using:

```text
Tag: v1.0.0
Title: v1.0.0 – Initial Public Release
```

Paste the contents of `RELEASE_NOTES_v1.0.0.md` into the release description and publish it as a normal release, not a pre-release.

## First update commit

Suggested commit message:

```text
docs: improve project presentation and add release assets
```

## Sharing

- Use `docs/LINKEDIN_POST.md` for a LinkedIn announcement.
- Use `docs/GITHUB_PROFILE_SNIPPET.md` in the profile README repository.
- Link this project from future related repositories.

## Screenshot note

`docs/screenshots/main-window-preview.png` is an illustrative preview based on the current Qt Widgets layout. Replace it with a verified runtime screenshot after building and testing the application on Windows with Npcap.
