#!/bin/bash
# Create APT repository structure for GitHub Pages
# Run this ON Raspberry Pi or Linux system

set -e

echo "=========================================="
echo "Creating APT Repository"
echo "=========================================="
echo ""

# Check if running on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo "WARNING: This script should be run on Linux/Raspberry Pi for best results."
    echo "         Current OS: $OSTYPE"
    read -p "Continue anyway? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Check for required tools
echo "Checking required tools..."
if ! command -v dpkg-scanpackages &> /dev/null; then
    echo "Installing dpkg-dev..."
    sudo apt-get update
    sudo apt-get install -y dpkg-dev
fi

# Build the .deb package first
echo ""
echo "Building .deb package..."
if [ -f "./build-on-pi.sh" ]; then
    ./build-on-pi.sh
else
    echo "ERROR: build-on-pi.sh not found. Make sure you're in the project directory."
    exit 1
fi

# Create repository structure
echo ""
echo "Creating repository structure..."
REPO_DIR="apt-repo"
rm -rf "$REPO_DIR"
mkdir -p "$REPO_DIR/pool/main"

# Copy .deb file
DEB_FILE=$(ls ../*.deb 2>/dev/null | head -1)
if [ -z "$DEB_FILE" ]; then
    echo "ERROR: No .deb file found in parent directory."
    exit 1
fi

echo "Found package: $DEB_FILE"
cp "$DEB_FILE" "$REPO_DIR/pool/main/"

# Generate Packages file
echo ""
echo "Generating Packages file..."
cd "$REPO_DIR"
dpkg-scanpackages pool/ /dev/null > Packages
gzip -k -f Packages

# Generate Release file
echo ""
echo "Generating Release file..."
cat > Release << EOF
Origin: OSOYOO DSI Panel Repository
Label: OSOYOO
Suite: stable
Codename: stable
Version: 1.0
Architectures: all arm64 armhf
Components: main
Description: OSOYOO DSI Panel Driver Repository
Date: $(date -R)
EOF

# Add checksums
echo "MD5Sum:" >> Release
md5sum Packages Packages.gz | sed 's/^/ /' >> Release

echo "SHA1:" >> Release
sha1sum Packages Packages.gz | sed 's/^/ /' >> Release

echo "SHA256:" >> Release
sha256sum Packages Packages.gz | sed 's/^/ /' >> Release

cd ..

echo ""
echo "✓ Repository created successfully!"
echo ""
echo "Repository contents:"
ls -lh "$REPO_DIR/pool/main/"
echo ""
ls -lh "$REPO_DIR/"

# Ask about GPG signing
echo ""
read -p "Do you want to GPG sign the repository? (recommended) (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "Available GPG keys:"
    gpg --list-keys
    echo ""
    read -p "Enter your email address for the GPG key: " EMAIL

    if gpg --list-keys "$EMAIL" &> /dev/null; then
        echo "Signing Release file..."
        cd "$REPO_DIR"

        # Create detached signature
        gpg --armor --detach-sign -o Release.gpg Release

        # Create clear-signed InRelease
        gpg --clearsign -o InRelease Release

        # Export public key
        gpg --armor --export "$EMAIL" > public.key

        cd ..

        echo "✓ Repository signed successfully!"
        echo ""
        echo "Public key exported to: $REPO_DIR/public.key"
    else
        echo "ERROR: No GPG key found for $EMAIL"
        echo ""
        echo "To create a GPG key, run:"
        echo "  gpg --gen-key"
        exit 1
    fi
fi

echo ""
echo "=========================================="
echo "APT Repository Setup Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo ""
echo "1. Commit the apt-repo directory:"
echo "   git add apt-repo/"
echo "   git commit -m 'Add APT repository'"
echo "   git push"
echo ""
echo "2. Enable GitHub Pages:"
echo "   - Go to: https://github.com/osoyoo/osoyoo-dsi-panel/settings/pages"
echo "   - Source: Deploy from a branch"
echo "   - Branch: master"
echo "   - Folder: /apt-repo"
echo "   - Click Save"
echo ""
echo "3. Wait a few minutes for GitHub Pages to deploy"
echo ""
echo "4. Users can then install with:"
echo "   echo 'deb [trusted=yes] https://osoyoo.github.io/osoyoo-dsi-panel/ ./' | \\"
echo "       sudo tee /etc/apt/sources.list.d/osoyoo.list"
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "   Or with GPG verification:"
    echo "   wget -qO - https://osoyoo.github.io/osoyoo-dsi-panel/public.key | sudo apt-key add -"
    echo "   echo 'deb https://osoyoo.github.io/osoyoo-dsi-panel/ ./' | \\"
    echo "       sudo tee /etc/apt/sources.list.d/osoyoo.list"
fi
echo ""
echo "   sudo apt update"
echo "   sudo apt install osoyoo-dsi-panel-dkms"
echo ""
echo "=========================================="
