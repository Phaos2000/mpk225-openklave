# Read-only MPK225 USB/PnP inventory on Windows 10/11.
# No MIDI/SysEx output, no firmware/update mode, no driver changes.
$ErrorActionPreference = 'Stop'
Write-Host "MPK225 USB inventory (read-only)" -ForegroundColor Cyan
Write-Host "Scanning Windows Plug and Play devices..."
try {
    $devices = @(Get-CimInstance -ClassName Win32_PnPEntity)
    $seed = @($devices | Where-Object {
        ([string]$_.Name -match 'MPK\s*225') -or
        ([string]$_.Description -match 'MPK\s*225')
    })

    $knownPairs = @()
    foreach ($device in $seed) {
        $id = [string]$device.PNPDeviceID
        $match = [regex]::Match($id, 'VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})')
        if ($match.Success) {
            $knownPairs += ($match.Groups[1].Value.ToUpperInvariant() + ':' +
                            $match.Groups[2].Value.ToUpperInvariant())
        }
    }
    $knownPairs = @($knownPairs | Select-Object -Unique)

    $found = @()
    foreach ($device in $devices) {
        $id = [string]$device.PNPDeviceID
        $match = [regex]::Match($id, 'VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})')
        $pair = ''
        if ($match.Success) {
            $pair = $match.Groups[1].Value.ToUpperInvariant() + ':' +
                    $match.Groups[2].Value.ToUpperInvariant()
        }
        if (($seed -contains $device) -or ($pair -and ($knownPairs -contains $pair))) {
            $interfaceMatch = [regex]::Match($id, '&MI_([0-9A-Fa-f]{2})')
            $interface = ''
            if ($interfaceMatch.Success) { $interface = $interfaceMatch.Groups[1].Value.ToUpperInvariant() }
            $found += [pscustomobject]@{
                Name = [string]$device.Name
                Manufacturer = [string]$device.Manufacturer
                PNPClass = [string]$device.PNPClass
                Status = [string]$device.Status
                Service = [string]$device.Service
                VID_PID = $pair
                Interface = $interface
            }
        }
    }

    $report = [ordered]@{
        device = 'Akai MPK225'
        source = 'Windows Win32_PnPEntity; read-only'
        contains_serial_or_instance_id = $false
        matches = @($found)
    }
    $path = Join-Path $PSScriptRoot 'mpk225-usb-sanitized.json'
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $path -Encoding UTF8
    Write-Host ""
    if ($found.Count -gt 0) {
        Write-Host ("FOUND: {0} matching device record(s)." -f $found.Count) -ForegroundColor Green
        $found | Format-Table Name, VID_PID, Interface, Status, Service -AutoSize | Out-Host
        Write-Host "Saved report: $path"
        exit 0
    }
    Write-Host "No MPK225-named device found in Windows PnP inventory." -ForegroundColor Yellow
    $candidates = @($devices | Where-Object {
        ([string]$_.Name -match 'Akai|MPK') -or
        ([string]$_.Manufacturer -match 'Akai')
    } | Select-Object -First 12 Name, Manufacturer, Status)
    if ($candidates.Count -gt 0) {
        Write-Host "Related device names:"
        $candidates | Format-Table -AutoSize | Out-Host
    }
    Write-Host "Check the USB cable/power and Windows Device Manager."
    Write-Host "The report is saved here: $path"
    exit 1
}
catch {
    Write-Host ""
    Write-Host "INVENTORY FAILED:" -ForegroundColor Red
    Write-Host $_.Exception.Message
    exit 2
}
