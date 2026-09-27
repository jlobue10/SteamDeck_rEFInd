# Code signing (Windows)

The Windows executable, the privileged helper, the bundled PowerShell scripts,
and the Inno Setup installer are Authenticode-signed through
**[SignPath Foundation](https://signpath.org/)**, which provides free
code-signing certificates to open-source projects. Signing happens in CI
(`.github/workflows/windows-release.yml`) — SignPath holds the private key in
its HSM and signs artifacts the workflow submits, after verifying they were
built by this repository's GitHub Actions workflow.

Until the setup below is complete, the release workflow still runs and produces
**unsigned** artifacts (the signing steps are skipped while the
`SIGNPATH_ORGANIZATION_ID` repository variable is empty). Users of unsigned
builds see a Windows SmartScreen / UAC "unknown publisher" warning.

## What SignPath Foundation requires of this repository

These are conditions of the free certificate
([signpath.org/terms](https://signpath.org/terms)); the repo already meets them
and they must stay true:

- **Attribution + policy section.** The README's *Code signing policy* section
  carries the required sentence ("Free code signing provided by SignPath.io,
  certificate by SignPath Foundation"), the team roles (Authors / Reviewers /
  Approvers) and the privacy statement. Keep it accurate — in particular the
  privacy statement's claim that all network access is user-initiated.
- **File metadata restrictions.** Every signed PE file must carry a
  `ProductName` equal to the project's name and a `ProductVersion` that is the
  same for every file in a build, and the artifact configuration must
  *enforce* both. Where those values come from:

  | Signed file | ProductName / ProductVersion source |
  |---|---|
  | `SteamDeck_rEFInd.exe` | `GUI/src/rEFInd_GUI.rc` ← `version_generated.h`, configured by CMake from `project(VERSION)` (`GUI/src/version.h.in`) |
  | `SteamDeck_rEFInd_helper.exe` | `GUI/src/helper.rc` ← same header |
  | `SteamDeck_rEFInd-<ver>-setup.exe` | `VersionInfoProductName` / `VersionInfoProductTextVersion` in `SteamDeck_rEFInd.iss` (from `AppVersion`) |

  `ProductName` is `SteamDeck_rEFInd` everywhere; `ProductVersion` is the
  plain `X.Y.Z` release version, which the workflow reads from the `VERSION`
  file and hands to SignPath as the `version` parameter. On tag builds the
  workflow fails early if the tag is not `v<VERSION>`, because a mismatch
  would make SignPath reject the signing request anyway. PowerShell scripts
  have no version resource, so no restriction applies to them.
- **Builds must be verifiable.** Only GitHub-hosted runners, only artifacts
  uploaded by the workflow (SignPath's connector checks both), no unsigned
  third-party executables passed off as ours. The Qt/MinGW runtime DLLs are
  upstream MSYS2 builds and ship unsigned — the terms allow that, and the
  README says so.
- **MFA** on GitHub and SignPath for everyone with a role; the **Approver**
  approves every release signing request by hand in the SignPath console.

## One-time setup

### 1. Apply to SignPath Foundation
Register the project at <https://signpath.org/apply> (OSI licenses such as
this repo's MIT qualify). The application for this repo was submitted on
2026-08-14. Once approved you get an **organization**, a **project**, and the
**signing policies** (`test-signing`, `release-signing`) in the SignPath web
console at <https://app.signpath.io>.

### 2. Create the two artifact configurations
SignPath signs the *contents* of an uploaded artifact according to an
"artifact configuration" — an XML document describing which files inside it
to sign and the metadata restrictions to enforce. The two this workflow needs
are versioned in this directory; create each in the console under
*Project → Artifact configurations → Add*, choose *XML*, and paste the file:

| Slug | File | Signs |
|---|---|---|
| `deploy-contents` | [`signpath/deploy-contents.xml`](signpath/deploy-contents.xml) | `SteamDeck_rEFInd.exe`, `SteamDeck_rEFInd_helper.exe`, `windows/*.ps1` (the four scripts, listed explicitly) inside the uploaded `deploy/` folder |
| `installer` | [`signpath/installer.xml`](signpath/installer.xml) | the single `SteamDeck_rEFInd-<ver>-setup.exe` |

Both declare a required `version` parameter and restrict `product-name` /
`product-version` on every PE file. Both start with `<zip-file>` because a
GitHub Actions artifact is always a ZIP with the uploaded folder's (or file's)
contents at its root. Files not listed (DLLs, plugin folders, icons, themes)
pass through untouched. **When either XML changes here, paste the new version
into the console — the repo copy is the source of truth but SignPath only
reads its own.** Validate edits against
<https://app.signpath.io/Web/artifact-configuration/v1.xsd> before pasting.

### 3. Connect this GitHub repository
1. In *Organization → Trusted build systems*, add the predefined
   **GitHub.com** connector (the Foundation usually pre-adds it).
2. In *Project → Trusted build systems*, link it to the project, and set the
   project's **repository URL** to `https://github.com/jlobue10/SteamDeck_rEFInd`.
3. Install the **SignPath GitHub App** for this repository when the console
   offers it (it lets SignPath read the workflow run for origin verification).
4. On the `release-signing` policy keep *manual approval* on and *verify
   origin* on; if it has an allowed-branches list, releases are built from
   tags `v*`, so the pattern must admit those (plus `main` if you want
   `workflow_dispatch` runs signed too).

SignPath then only signs artifacts produced by this repository's own workflow
runs on GitHub-hosted runners.

### 4. API token and repository settings
Create a SignPath **API token** for a user who is a *Submitter* on both signing
policies (SignPath recommends a dedicated CI user; for the Foundation the
project owner's own token is the norm). Then in
**GitHub → Settings → Secrets and variables → Actions**:

Secret:
- `SIGNPATH_API_TOKEN` — that token.

Variables (Variables tab, not Secrets):

| Variable | Value |
|---|---|
| `SIGNPATH_ORGANIZATION_ID` | the organization GUID (console URL / *Organization* page) |
| `SIGNPATH_PROJECT_SLUG` | `SteamDeck_rEFInd` (whatever slug the project shows) |
| `SIGNPATH_POLICY_SLUG` | `test-signing` for the first run, then `release-signing` |
| `SIGNPATH_DEPLOY_CONFIG_SLUG` | `deploy-contents` |
| `SIGNPATH_INSTALLER_CONFIG_SLUG` | `installer` |

Setting `SIGNPATH_ORGANIZATION_ID` is what turns signing on. Leave it unset to
keep producing unsigned builds.

### 5. First run
Trigger *Windows Release* by hand (`workflow_dispatch` on `main`) with
`SIGNPATH_POLICY_SLUG=test-signing`. That exercises the whole pipeline with a
self-signed test certificate and no approval step; a failure here is almost
always the artifact configuration (a path or restriction that doesn't match
the upload) and the signing request page in the console says which file. When
it passes, switch the variable to `release-signing`; the next `v*` tag then
pauses at *Sign deploy contents* until an Approver approves the request in the
console (twice per release: once per stage), and the workflow's default
10-minute wait (`wait-for-completion-timeout-in-seconds`) covers a prompt
approval — raise it if approvals are slow.

## How the workflow uses these

1. Read the release version from `VERSION` (must be `X.Y.Z`; on a tag, must
   equal the tag minus its `v`).
2. Build + assemble `deploy/`.
3. **Stage A** — upload `deploy/` as a workflow artifact and submit it under
   `deploy-contents` with `version=<VERSION>`; SignPath verifies the origin,
   checks `ProductName`/`ProductVersion` on both exes, and returns the folder
   with the exes and the `.ps1` scripts signed. These replace the unsigned
   copies.
4. Build the installer from the now-signed `deploy/` (after checking the Inno
   Setup compiler's own Authenticode signature).
5. **Stage B** — upload the setup exe and submit it under `installer` with the
   same `version`; the signed installer replaces the unsigned one.
6. Attach the signed installer (+ `.sha256`) to the release.

The build job grants the workflow token `actions: read` in addition to
`contents: read`: SignPath's connector uses that token to read the job and
download the uploaded artifact.

## Verifying a signature locally

```powershell
Get-AuthenticodeSignature .\SteamDeck_rEFInd.exe | Format-List Status, SignerCertificate
(Get-Item .\SteamDeck_rEFInd.exe).VersionInfo | Format-List ProductName, ProductVersion, FileVersion
signtool verify /pa /v .\SteamDeck_rEFInd-3.4.4-setup.exe   # if the Windows SDK is installed
```

`Status = Valid` with the SignPath-issued certificate as signer means the
artifact is properly signed; `ProductName = SteamDeck_rEFInd` and
`ProductVersion = <release>` are what the artifact configuration enforced.
Reputation with SmartScreen accrues over time as signed downloads accumulate;
an EV certificate (not free) would grant it immediately, but the Foundation OV
certificate is the standard OSS choice.

## Changing things later

- **Version bump:** the resources follow `project(VERSION)` in
  `GUI/src/CMakeLists.txt` and the installer follows `AppVersion` in the
  `.iss`; `VERSION` must match both or signing fails. See the version-carrier
  list in `CLAUDE.md`.
- **New or renamed `.ps1` in `Windows/GUI/`:** add it to
  `signpath/deploy-contents.xml` *and* paste the updated XML into the console,
  or Stage A fails with an unmatched file.
- **New executable in `deploy/`:** give it a VERSIONINFO resource fed from
  `version_generated.h` and add it to the `pe-file-set` with the same
  restrictions.
- **Sibling repo:** rEFInd_GUI needs the identical treatment (its own
  application, resources, XMLs and README section) — nothing here is shared
  with it automatically.
