# Code signing (Windows)

The Windows executable, the Inno Setup installer, and the bundled PowerShell
scripts are Authenticode-signed through **[SignPath Foundation](https://signpath.org/)**,
which provides free code-signing certificates to open-source projects. Signing
happens in CI (`.github/workflows/windows-release.yml`) — SignPath holds the
private key in its HSM and signs artifacts your workflow submits, after
verifying they were built by this repository.

Until the setup below is complete, the release workflow still runs and produces
**unsigned** artifacts (the signing steps are skipped when the
`SIGNPATH_ORGANIZATION_ID` repository variable is empty). Users of unsigned
builds will see a Windows SmartScreen / UAC "unknown publisher" warning.

## One-time setup

### 1. Apply to SignPath Foundation
Register the project at <https://about.signpath.io/product/open-source> (GPL-3.0
qualifies). Once approved you get an **organization**, a **project**, and a
**signing policy** in the SignPath web console.

### 2. Connect this GitHub repository
In the SignPath console, add the **GitHub Actions** trusted build system and
authorize this repo, so SignPath will only sign artifacts produced by this
repo's workflows.

### 3. Create two artifact configurations
SignPath signs the *contents* of an uploaded artifact according to an "artifact
configuration". Create two:

- **`deploy-contents`** — signs the files inside the uploaded `deploy` folder:
  - `SteamDeck_rEFInd.exe` (Authenticode / PE)
  - `windows/*.ps1` (Authenticode / PowerShell)
- **`installer`** — signs the single uploaded `SteamDeck_rEFInd-<ver>-setup.exe`
  (Authenticode / PE).

### 4. Add the repository secret and variables
In **GitHub → Settings → Secrets and variables → Actions**:

Secret:
- `SIGNPATH_API_TOKEN` — a SignPath user API token.

Variables (Variables tab, not Secrets):
| Variable | Value (example) |
|---|---|
| `SIGNPATH_ORGANIZATION_ID` | your SignPath organization GUID |
| `SIGNPATH_PROJECT_SLUG` | `SteamDeck_rEFInd` |
| `SIGNPATH_POLICY_SLUG` | `release-signing` |
| `SIGNPATH_DEPLOY_CONFIG_SLUG` | `deploy-contents` |
| `SIGNPATH_INSTALLER_CONFIG_SLUG` | `installer` |

Setting `SIGNPATH_ORGANIZATION_ID` is what turns signing on. Leave it unset to
keep producing unsigned builds.

## How the workflow uses these

1. Build + assemble `deploy/`.
2. **Stage A** — upload `deploy/` and submit it under `deploy-contents`;
   SignPath returns the folder with `SteamDeck_rEFInd.exe` and the `.ps1` scripts
   signed. These replace the unsigned copies.
3. Build the installer from the now-signed `deploy/`.
4. **Stage B** — upload the setup exe and submit it under `installer`; the
   signed installer replaces the unsigned one.
5. Attach the signed installer to the release.

## Verifying a signature locally

```powershell
Get-AuthenticodeSignature .\SteamDeck_rEFInd.exe | Format-List Status, SignerCertificate
signtool verify /pa /v .\SteamDeck_rEFInd-2.0.0-setup.exe   # if the Windows SDK is installed
```

`Status = Valid` with the SignPath-issued certificate as signer means the
artifact is properly signed. Reputation with SmartScreen accrues over time as
signed downloads accumulate; an EV certificate (not free) would grant it
immediately, but the Foundation OV certificate is the standard OSS choice.
