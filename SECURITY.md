# Security policy

**How to report a security problem in rs7string.**

## Supported versions

| Version | Supported |
|---|---|
| 1.x | Yes |
| < 1.0 | No |

## Reporting a vulnerability

> [!IMPORTANT]
> Please report security problems privately. Do not open a public issue.

1. Open the **Security** tab of this repository on GitHub.
2. Choose **Report a vulnerability** (GitHub private vulnerability reporting).
3. Describe the problem, the affected version, and the steps to reproduce it.

You will get an answer as soon as possible. Please give us time to fix the problem before you share
it publicly.

## Scope

| In scope | Out of scope |
|---|---|
| The rs7string DLL (`dinput8.dll`) and the tools in this repository. | The game itself (Rocksmith 2014). |
| Memory-safety bugs. | Other mods such as RSMods, RS_ASIO or CDLC enablers. Report those to their own projects. |
| Unsafe file handling. | Extraction of encryption keys, DRM circumvention, or decryption of official game archives. This project does not support these topics, and such reports will not be handled. |
| A way to make the DLL do something it should not. | |
