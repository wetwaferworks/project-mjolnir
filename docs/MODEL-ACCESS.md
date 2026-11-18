# Model access

Decided by JD on November 17, 2026.

When Mjolnir needs a hosted model (for example to help grade, or to compare
answers), it signs in with that provider's API key. There is no OAuth sign-in
flow.

- Each key is entered once, by JD, on the machine that uses it.
- It is stored in Windows Credential Manager, under the user's own Windows
  account, and read from there when needed.
- Keys never appear in this repository, in commits, in logs, in ledgers or in
  reports, and are never passed on the command line.
- Removing a key from Credential Manager turns that provider off.

This is independent of PCHIE and of any PCHIE sign-in design. Nothing in
PCHIE changes because of it, and stage M1 does not call any hosted model yet.