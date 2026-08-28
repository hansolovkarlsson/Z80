# Postmortems

Write-ups of failures worth understanding on their own — the ones whose
lesson is bigger than the diff that fixed them.

The bar for a file here is not "a bug was found." Most bugs belong in a
commit message and, if they were instructive, a line in
[`../JOURNAL.md`](../JOURNAL.md). A postmortem is for a failure that
teaches something reusable: a class of mistake this project is
structurally prone to, a gap in how something gets verified, or a wrong
assumption that would have gone on being made.

Each one answers the same four questions:

1. **What happened** — the observable failure, in the terms it was first
   seen in.
2. **Root cause** — what was actually wrong, as distinct from what it
   looked like.
3. **Why it survived** — the more useful question. What about the tests,
   the tools, or the habits let this go unnoticed?
4. **What changed** — the fix, and separately, whatever now makes the
   *class* of problem harder to reintroduce.

| Date | Postmortem | One-line lesson |
|---|---|---|
| 2026-08-29 | [The test that matched its own input](2026-08-29-test-matched-the-echoed-input.md) | A check is finished when it has been seen to fail for the right reason, not when it passes |
| 2026-08-28 | [A status bit invented from one ROM](2026-08-28-status-bit-invented-from-one-rom.md) | A field the only consumer never reads cannot be validated by any test — only by a second consumer |
| 2026-08-28 | [The boot screen cannot validate the feature](2026-08-28-boot-screen-cannot-validate.md) | A test whose input never exercises the code proves nothing, however real the input is |
| 2026-08-28 | [`--type` fed raw UTF-8 bytes](2026-08-28-type-raw-utf8-bytes.md) | Two paths doing "the same" thing will disagree unless one of them calls the other |
| 2026-08-28 | [The DART's single receive byte](2026-08-28-dart-single-byte-overwrite.md) | Emulating a hardware limit means honoring it on *every* input path, not just the one it was found on |
| 2026-08-18 | [Block I/O opcodes missing from the shared core](2026-08-28-block-io-opcodes-missing.md) | An oracle is only as good as its coverage — ZEXALL exercises no I/O at all |
