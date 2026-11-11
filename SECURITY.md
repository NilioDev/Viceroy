# Security

## Reporting a problem

Please do not open a public issue for a security problem. Use
[private vulnerability reporting](https://github.com/NilioDev/Viceroy/security/advisories/new)
instead. You get an answer within a week.

## What counts

Viceroy reads files it did not make: the original game files, savegames and,
later, mods that people share. A file that makes the program crash, read or
write outside a buffer, hang, or touch files outside its own folders is a
security problem, even if it needs a specially made file.

## Rules for the code

- Every reader of an outside file checks each size, count and offset against
  the real length of the data before it uses it, and fails with an error
  instead of crashing.
- Paths inside mods and archives are never trusted: no absolute paths, no
  `..`, and nothing is written outside the app's data folder.
- No code is downloaded or run at runtime. Mods are data only.
- The build downloads only pinned versions (SDL by release tag, GitHub
  Actions by commit hash).
