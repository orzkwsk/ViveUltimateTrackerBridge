# Research-only direct-to-file Health Gate. Run this entire script in its own elevated console.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$USBPcapPath,
    [Parameter(Mandatory=$true)][string]$ControlDevice,
    [Parameter(Mandatory=$true)][string]$DeviceAddress,
    [Parameter(Mandatory=$true)][int]$TargetBus,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [Parameter(Mandatory=$true)][string]$TsharkPath,
    [Parameter(Mandatory=$true)][string]$ConsoleContractReceiptPath,
    [int]$DurationSeconds=5,
    [switch]$InjectDescriptors,
    [int]$FileTimeoutSeconds=5,
    [int]$ExitTimeoutSeconds=10
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'usbpcap_health_contract.psm1') -Force
$privateRoot=Join-Path (Split-Path $PSScriptRoot -Parent) 'hil-private'
$config=@{ USBPcapPath=$USBPcapPath; ControlDevice=$ControlDevice; DeviceAddress=$DeviceAddress;
    TargetBus=$TargetBus; OutputPath=$OutputPath; ReceiptPath=$OutputPath+'.receipt.json';
    TsharkPath=$TsharkPath; DurationSeconds=$DurationSeconds; InjectDescriptors=[bool]$InjectDescriptors;
    FileTimeoutSeconds=$FileTimeoutSeconds; ExitTimeoutSeconds=$ExitTimeoutSeconds; PrivateRoot=$privateRoot;
    OfflineTest=$false; ConsoleContractReceiptPath=$ConsoleContractReceiptPath }
try {
    $receipt=Invoke-UsbHealthCapture $config
    ConvertTo-PublicHealthReceipt $receipt | ConvertTo-Json -Depth 5 | Write-Output
    if ($receipt.Result -eq 'PASS') { exit 0 }
    exit 1
} catch { Write-Output 'LAUNCHER_PREFLIGHT_FAILED (see private receipt if reserved)'; exit 2 }
