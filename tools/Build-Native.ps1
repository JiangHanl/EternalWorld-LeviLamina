param([switch]$HostOnly)
# Compatibility command for the new Host + internal modules architecture.
& (Join-Path $PSScriptRoot 'Build-Portable.ps1') -HostOnly:$HostOnly
