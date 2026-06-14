# Security and publishing checklist

Before publishing a networking project, verify all of the following:

- No production IP addresses, domains, ports, usernames, tokens, or credentials are present.
- No proprietary packet identifiers or device command formats are present.
- No company names, customer names, serial numbers, calibration files, or screenshots are present.
- Build directories, executable files, DLL files, logs, captures, and IDE user settings are ignored.
- The repository contains only code you are authorized to publish.
- Npcap binaries are not committed to the repository.

This demo uses generic defaults and contains no code for a specific FPGA or instrument protocol.
