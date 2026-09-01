# Signing release tags

Release builds clone a git **tag**, which is mutable — anyone with push access
could move `vX.Y.Z` to a different commit and CI would repackage it. Signing the
tag and verifying the signature at build time closes that gap.

This is **opt-in and non-breaking**: until a real public key is committed to
`.github/release-signing-key.asc`, the "Verify release tag signature" step in
every release workflow only prints a warning and continues, and the PKGBUILD's
`validpgpkeys` line stays commented. Nothing changes for the current pipeline
(whose existing tags are unsigned) until you deliberately turn it on.

## One-time setup

1. **Create a signing key** (if you don't already have one):

   ```
   gpg --full-generate-key        # choose ECC (ed25519) or RSA 4096
   gpg --list-secret-keys --keyid-format=long   # note the fingerprint
   ```

2. **Commit the PUBLIC key** (never the private key) so builds can verify:

   ```
   gpg --armor --export <FINGERPRINT> > .github/release-signing-key.asc
   git add .github/release-signing-key.asc && git commit -m "Add release signing public key"
   ```

   The file must contain a real `-----BEGIN PGP PUBLIC KEY BLOCK-----`; that is
   what flips the CI step from warn-only to enforcing.

3. **Enable AUR/local enforcement** (optional but recommended): in `PKGBUILD`,
   uncomment `validpgpkeys=(...)` and set it to your key's full fingerprint.
   `makepkg` then refuses to build unless the cloned tag is signed by that key.

## Every release

Create the tag as an **annotated, signed** tag (lightweight tags cannot be
signed):

```
git tag -s vX.Y.Z -m "vX.Y.Z"
git push origin vX.Y.Z
```

Or set it once globally: `git config --global tag.gpgSign true` (then `git tag`
signs automatically). Confirm locally with `git verify-tag vX.Y.Z`.

## Notes

- **Existing unsigned tags won't verify.** Once the key is committed, rebuilding
  a pre-signing release (e.g. an old `workflow_dispatch` on `vX.Y.Z`) will fail
  the verify step — that is expected. Enforce going forward; don't retro-rebuild
  unsigned tags, or sign them retroactively (`git tag -s -f`, which rewrites the
  tag) if you must.
- The CI step imports the committed public key into an ephemeral runner keyring
  and runs `git verify-tag`. It does not need any secret.
- This is independent of Authenticode/SignPath binary signing (see
  `Windows/GUI/SIGNING.md`); tag signing protects the *source* a build packages,
  binary signing protects the *artifact* users download.
