$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Set-Location -LiteralPath $PSScriptRoot

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

Write-Host "AT32 Motion Studio - Windows Build" -ForegroundColor Cyan
Write-Host "===================================="
Require-Command "node"
Require-Command "npm"
Require-Command "rustc"
Require-Command "cargo"

Write-Host "[1/7] Node:  $(node --version)"
Write-Host "[2/7] npm:   $(npm --version)"
Write-Host "[3/7] Rust:  $(rustc --version)"

Write-Host "[4/7] Installing dependencies..."
npm install

Write-Host "[5/7] Source validation, TypeScript check and lint..."
npm run audit:firmware
npm run validate:source
npm run check
npm run lint

Write-Host "[6/7] Rust protocol regression tests and frontend build..."
npm run test:rust
npm run build

Write-Host "[7/7] Building Tauri MSI/NSIS bundles..."
npm run tauri build

Write-Host ""
Write-Host "Build completed." -ForegroundColor Green
Write-Host "Artifacts: src-tauri\target\release\bundle"
