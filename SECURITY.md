# Security policy

This is a research tool that implements an NFC relay (ISO 14443-4 / ISO-DEP). It is
meant for authorised testing and education only; see [docs/legal.md](docs/legal.md).

## Reporting a vulnerability

If you find a security problem in this code (for example a memory-safety bug in the FAP,
or a flaw in the relay transport), report it privately through this repository's GitHub
Security Advisories ("Report a vulnerability" under the Security tab). Use a public issue
only for low-risk items. Please do not include working exploit details against
third-party production systems.

## Scope

In scope: bugs in this repository's FAP, ESP32 bridge and Android reader.

Out of scope: help using the tool against systems you are not authorised to test. Such
requests will not be answered.
