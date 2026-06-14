# GitHub publishing checklist

## Repository name

`qt-udp-frame-reassembler`

## Description

`Qt and C++ UDP fragment reassembly with out-of-order handling, interleaved batches, duplicate suppression, CRC32 validation, and timeouts.`

## Topics

Paste these one at a time or as space-separated topics:

```text
qt qt6 cpp udp packet-reassembly packet-processing networking fpga embedded-systems data-acquisition crc32 cmake
```

## Suggested first commit

```text
feat: add reusable Qt UDP frame reassembler
```

## Suggested release

- Tag: `v1.0.0`
- Title: `v1.0.0 – Initial Public Release`
- Body: copy `RELEASE_NOTES_v1.0.0.md`

## Before publishing

1. Build the project with your local Qt kit.
2. Run `frame_reassembler_tests` or `ctest`.
3. Launch the demo and test all six scenarios.
4. Capture one real screenshot for the README.

## Social preview

Upload this file in GitHub under `Settings → General → Social preview → Edit`:

```text
docs/assets/social-preview.png
```

It is already 1280×640 and under 1 MB.
