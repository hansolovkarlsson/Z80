#!/usr/bin/env bash
# asm/vscode/install.sh - packages this extension as a .vsix and installs
# it into VS Code. Rerun it after changing anything here: an installed
# extension is a copy, not a link (VS Code only loads what its own
# registry lists, so linking the folder in does nothing).
#
# A .vsix is a zip holding the extension under extension/ plus two small
# XML files, so this builds one with Python's zipfile rather than needing
# Node and Microsoft's vsce.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$(mktemp -d)/z80asm.vsix"

python3 - "$HERE" "$OUT" <<'PY'
import json, os, sys, zipfile
here, out = sys.argv[1], sys.argv[2]
pkg = json.load(open(os.path.join(here, "package.json")))
files = ["package.json", "language-configuration.json", "extension.js", "lib.js",
         "keywords.json", "syntaxes/z80asm.tmLanguage.json", "README.md"]
manifest = f"""<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="{pkg['name']}" Version="{pkg['version']}" Publisher="{pkg['publisher']}"/>
    <DisplayName>{pkg['displayName']}</DisplayName>
    <Description xml:space="preserve">{pkg['description']}</Description>
    <Categories>Programming Languages</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="{pkg['engines']['vscode']}"/>
    </Properties>
  </Metadata>
  <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code"/></Installation>
  <Dependencies/>
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true"/>
  </Assets>
</PackageManifest>
"""
types = """<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json"/>
  <Default Extension=".js" ContentType="application/javascript"/>
  <Default Extension=".md" ContentType="text/markdown"/>
  <Default Extension=".vsixmanifest" ContentType="text/xml"/>
</Types>
"""
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", types)
    z.writestr("extension.vsixmanifest", manifest)
    for f in files:
        z.write(os.path.join(here, f), "extension/" + f)
PY

code --install-extension "$OUT" --force
rm -rf "$(dirname "$OUT")"
echo "Installed. Reload any open VS Code window (Developer: Reload Window) to pick it up."
