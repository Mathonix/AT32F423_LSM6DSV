$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Set-Location -LiteralPath $PSScriptRoot
if (-not (Test-Path -LiteralPath "node_modules")) { npm install }
npm run validate:source
npm run dev:desktop
