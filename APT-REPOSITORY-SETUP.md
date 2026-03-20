# APT Repository Setup

This document explains how the APT repository is set up for automatic installation via `sudo apt install`.

## Overview

The APT repository allows users to install the driver with:

```bash
echo "deb [trusted=yes] https://osoyoo.github.io/osoyoo-dsi-panel/ ./" | \
    sudo tee /etc/apt/sources.list.d/osoyoo.list

sudo apt update
sudo apt install osoyoo-dsi-panel-dkms
```

## Automated Setup (Recommended)

The repository is **automatically built and deployed** using GitHub Actions when you push to the master branch.

### How It Works

1. **Push to master** → Triggers GitHub Actions workflow
2. **Workflow runs** on Ubuntu (Linux):
   - Builds .deb package using `dpkg-buildpackage`
   - Creates APT repository structure
   - Generates `Packages` and `Release` files
   - Deploys to GitHub Pages (`gh-pages` branch)
3. **GitHub Pages** serves the repository at: `https://osoyoo.github.io/osoyoo-dsi-panel/`

### Enable GitHub Actions

The workflow is already committed (`.github/workflows/build-apt-repo.yml`).

To enable it:

1. **Enable GitHub Pages**:
   - Go to: https://github.com/osoyoo/osoyoo-dsi-panel/settings/pages
   - Source: **Deploy from a branch**
   - Branch: **gh-pages** (will be created by workflow)
   - Folder: **/ (root)**
   - Click **Save**

2. **Enable GitHub Actions** (if not already enabled):
   - Go to: https://github.com/osoyoo/osoyoo-dsi-panel/settings/actions
   - Allow all actions and reusable workflows

3. **Push changes** to trigger the workflow:
   ```bash
   git push
   ```

4. **Monitor the workflow**:
   - Go to: https://github.com/osoyoo/osoyoo-dsi-panel/actions
   - Wait for "Build APT Repository" workflow to complete (~2-3 minutes)

5. **Verify deployment**:
   - Visit: https://osoyoo.github.io/osoyoo-dsi-panel/
   - Should see the repository index page

---

## Manual Setup (Alternative)

If you prefer to build manually on a Raspberry Pi:

### On Raspberry Pi:

```bash
# Run the setup script
./create-apt-repo.sh
```

This will:
1. Build the .deb package
2. Create `apt-repo/` directory
3. Generate Packages and Release files
4. Optionally GPG sign the repository

### Commit and Push:

```bash
git add apt-repo/
git commit -m "Add APT repository"
git push
```

### Enable GitHub Pages:

1. Go to: https://github.com/osoyoo/osoyoo-dsi-panel/settings/pages
2. Source: Deploy from a branch
3. Branch: **master**
4. Folder: **/apt-repo**
5. Click Save

Wait a few minutes for deployment, then test.

---

## Repository Structure

Once deployed, the repository has this structure:

```
https://osoyoo.github.io/osoyoo-dsi-panel/
├── index.html                    # Repository info page
├── Packages                      # Package metadata
├── Packages.gz                   # Compressed package metadata
├── Release                       # Repository release file
├── Release.gpg (optional)        # GPG signature
├── InRelease (optional)          # Clear-signed release
├── public.key (optional)         # GPG public key
└── pool/
    └── main/
        └── osoyoo-dsi-panel-dkms_1.0-1_all.deb
```

---

## User Installation

Once the repository is live, users install with:

### Without GPG Verification (Simpler):

```bash
echo "deb [trusted=yes] https://osoyoo.github.io/osoyoo-dsi-panel/ ./" | \
    sudo tee /etc/apt/sources.list.d/osoyoo.list

sudo apt update
sudo apt install osoyoo-dsi-panel-dkms
```

### With GPG Verification (If signed):

```bash
wget -qO - https://osoyoo.github.io/osoyoo-dsi-panel/public.key | sudo apt-key add -

echo "deb https://osoyoo.github.io/osoyoo-dsi-panel/ ./" | \
    sudo tee /etc/apt/sources.list.d/osoyoo.list

sudo apt update
sudo apt install osoyoo-dsi-panel-dkms
```

---

## Updating the Repository

### Automated (GitHub Actions):

Simply push changes to master:

```bash
git add .
git commit -m "Update driver"
git push
```

The workflow automatically rebuilds and redeploys.

### Manual:

If using manual setup:

1. Make code changes
2. Run `./create-apt-repo.sh` again
3. Commit and push `apt-repo/` directory

---

## Troubleshooting

### Workflow Fails

**Check the workflow logs:**
- Go to: https://github.com/osoyoo/osoyoo-dsi-panel/actions
- Click on the failed run
- Check the logs for errors

**Common issues:**
- Missing GitHub Pages permissions
- Build errors in debian/rules
- Missing dependencies

### Repository Returns 404

**Check GitHub Pages is enabled:**
- Settings → Pages → Should show the deployed URL

**Check the gh-pages branch exists:**
```bash
git fetch origin
git branch -r
# Should see: origin/gh-pages
```

**Check the deployed files:**
- Visit: https://osoyoo.github.io/osoyoo-dsi-panel/Packages
- Should download the Packages file

### Users Can't Install

**Test the repository manually:**

```bash
# On Raspberry Pi
wget https://osoyoo.github.io/osoyoo-dsi-panel/Packages
cat Packages
# Should show package metadata
```

**Check DNS:**
```bash
nslookup osoyoo.github.io
# Should resolve to GitHub's IPs (185.199.108.153, etc.)
```

**Check firewall:**
- Ensure port 443 is open
- Try without proxy/VPN

---

## Security Considerations

### GPG Signing (Optional but Recommended)

To sign the repository:

**1. Generate GPG key (if you don't have one):**
```bash
gpg --gen-key
```

**2. Export public key:**
```bash
gpg --armor --export support@osoyoo.info > apt-repo/public.key
```

**3. Sign Release file:**
```bash
cd apt-repo
gpg --clearsign -o InRelease Release
gpg --armor --detach-sign -o Release.gpg Release
cd ..
```

**4. Users import key before adding repository:**
```bash
wget -qO - https://osoyoo.github.io/osoyoo-dsi-panel/public.key | sudo apt-key add -
```

### Without GPG Signing

Using `[trusted=yes]` bypasses signature verification:
```bash
echo "deb [trusted=yes] https://osoyoo.github.io/osoyoo-dsi-panel/ ./" | \
    sudo tee /etc/apt/sources.list.d/osoyoo.list
```

**Note:** This is acceptable for first-party repositories served over HTTPS.

---

## Monitoring

### Check Repository Status

```bash
# Check if package is listed
curl -s https://osoyoo.github.io/osoyoo-dsi-panel/Packages | grep "Package:"

# Check package version
curl -s https://osoyoo.github.io/osoyoo-dsi-panel/Packages | grep "Version:"

# Check last update
curl -s https://osoyoo.github.io/osoyoo-dsi-panel/Release | grep "Date:"
```

### Download Statistics

Check GitHub repository Insights → Traffic for page views.

---

## Advanced: Multiple Versions

To support multiple package versions:

1. Keep old packages in `pool/main/`
2. Regenerate Packages file (includes all versions)
3. APT will install the latest by default
4. Users can install specific versions:
   ```bash
   sudo apt install osoyoo-dsi-panel-dkms=1.0-1
   ```

---

## Comparison: APT vs Direct Install

| Feature | APT Repository | Direct Install |
|---------|----------------|----------------|
| Installation | `apt install` | Run script |
| Updates | `apt upgrade` | Manual |
| Setup Complexity | Moderate (one-time) | None |
| User Experience | Professional | Simple |
| Dependencies | Auto-resolved | Manual |
| Version Control | Yes | No |

**Recommendation:** Use APT repository for production; direct install for development/testing.

---

## Support

- GitHub Actions Docs: https://docs.github.com/en/actions
- GitHub Pages Docs: https://docs.github.com/en/pages
- Debian Repository Format: https://wiki.debian.org/DebianRepository/Format
- Issues: https://github.com/osoyoo/osoyoo-dsi-panel/issues
- Email: support@osoyoo.info
