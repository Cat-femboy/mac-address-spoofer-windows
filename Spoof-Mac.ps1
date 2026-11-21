# MAC spoofer engine. The GUI calls this with -Headless; the .bat runs it interactively.\r\n[CmdletBinding()]
param (
    [string]$AdapterName,
    [string]$Mac,
    [switch]$Spoof,
    [switch]$Restore,
    [switch]$Revive,
    [switch]$RandomizeHostname,
    [switch]$ListAdapters,
    [switch]$Headless
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

function Test-IsAdmin {
    ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-RegistryAdapterPath {
    param ([string]$Guid)
    $want = $Guid.ToString().Trim('{','}').ToLowerInvariant()
    $classPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e972-e325-11ce-bfc1-08002be10318}'
    foreach ($k in (Get-ChildItem -Path $classPath -ErrorAction SilentlyContinue)) {
        $props = Get-ItemProperty -Path $k.PSPath -ErrorAction SilentlyContinue
        if ($props) {
            $have = [string]$props.NetCfgInstanceId
            if ($have -and $have.Trim().ToLowerInvariant() -eq $want) { return $k.PSPath }
        }
    }
    return $null
}

function New-ValidLAA-Mac {
    # Unicast + locally administered: bit0 = 0, bit1 = 1 of the first octet.
    $hi = Get-Random -InputObject @('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F')
    $lo = Get-Random -InputObject @('2','6','A','E')
    $tail = 1..5 | ForEach-Object { '{0:X2}' -f (Get-Random -Min 0 -Max 256) }
    return ($hi + $lo + ($tail -join ''))
}

function Normalize-Mac {
    param ([string]$s)
    return ($s -replace '[^0-9A-Fa-f]', '').ToUpperInvariant()
}

function Test-MacString {
    param ([string]$s)
    return ($s -match '^[0-9A-Fa-f]{12}$' -or $s -match '^([0-9A-Fa-f]{2}[-:]){5}[0-9A-Fa-f]{2}$')
}

function Get-Adapters {
    $raw = Get-NetAdapter -IncludeHidden -ErrorAction SilentlyContinue | Where-Object {
        $_.PhysicalMediaType -match '802\.3|Ethernet|Native 802\.11' -or $_.HardwareInterface -eq $true
    }
    $list = @()
    foreach ($a in $raw) {
        $pnp = Get-PnpDevice -InstanceId $a.PnpDeviceID -ErrorAction SilentlyContinue
        $isDisabled = ($pnp -and $pnp.Problem -eq 'CM_PROB_DISABLED')
        $status = if ($isDisabled) { 'Disabled' } else { [string]$a.Status }
        $list += [PSCustomObject]@{
            Name                 = $a.Name
            InterfaceDescription = $a.InterfaceDescription
            Status               = $status
            RawStatus            = [string]$a.Status
            IsDisabled           = [bool]$isDisabled
            MacAddress           = [string]$a.MacAddress
            InterfaceGuid        = $a.InterfaceGuid
            PnpDeviceID          = [string]$a.PnpDeviceID
            MediaType            = [string]$a.PhysicalMediaType
        }
    }
    return $list
}

function Restart-AdapterSafe {
    param ([string]$Name, [string]$PnpId)
    Write-Host "[*] Restarting adapter '$Name'..." -ForegroundColor Cyan
    if ($PnpId) {
        $dev = Get-PnpDevice -InstanceId $PnpId -ErrorAction SilentlyContinue
        if ($dev -and $dev.Problem -eq 'CM_PROB_DISABLED') {
            Write-Host "[*] Waking hardware from PnP disabled state..." -ForegroundColor Yellow
            Enable-PnpDevice -InstanceId $PnpId -Confirm:$false -ErrorAction SilentlyContinue
            Start-Sleep -Seconds 2
        }
    }
    try {
        Restart-NetAdapter -Name $Name -Confirm:$false -ErrorAction Stop
    } catch {
        Disable-NetAdapter -Name $Name -Confirm:$false -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
        Enable-NetAdapter -Name $Name -Confirm:$false -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 3
    if ($PnpId) {
        $dev = Get-PnpDevice -InstanceId $PnpId -ErrorAction SilentlyContinue
        if ($dev -and $dev.Problem -eq 'CM_PROB_DISABLED') {
            Enable-PnpDevice -InstanceId $PnpId -Confirm:$false -ErrorAction SilentlyContinue
        }
    }
    Enable-NetAdapter -Name $Name -Confirm:$false -ErrorAction SilentlyContinue
}

function Update-DhcpLease {
    param ([string]$Name)
    $v = Get-NetAdapter -Name $Name -ErrorAction SilentlyContinue
    if (-not $v -or $v.Status -ne 'Up') {
        Write-Host "[*] Link down - new identity presents itself when connected." -ForegroundColor DarkGray
        return
    }
    Write-Host "[*] Releasing current DHCP lease..." -ForegroundColor Cyan
    ipconfig /release "$Name" 2>&1 | Out-Null
    Write-Host "[*] Flushing DNS resolver cache..." -ForegroundColor Cyan
    ipconfig /flushdns 2>&1 | Out-Null
    Start-Sleep -Milliseconds 600
    Write-Host "[*] Requesting fresh IP lease..." -ForegroundColor Cyan
    ipconfig /renew "$Name" 2>&1 | Out-Null
}

if ($ListAdapters) {
    foreach ($a in (Get-Adapters)) {
        $clean = @($a.Name, $a.InterfaceDescription, $a.MacAddress, $a.Status,
                   $a.InterfaceGuid, $a.PnpDeviceID, $a.IsDisabled) | ForEach-Object {
            ([string]$_ -replace '\|', '/' -replace "(`r|`n|`t)", ' ').Trim()
        }
        Write-Host ("ADAPTER|{0}|{1}|{2}|{3}|{4}|{5}|{6}" -f $clean[0],$clean[1],$clean[2],$clean[3],$clean[4],$clean[5],$clean[6])
    }
    Write-Host 'END'
    exit 0
}

if (-not $Headless -and -not (Test-IsAdmin)) {
    Write-Host "[*] Requesting Administrator privileges..." -ForegroundColor Yellow
    $a = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    if ($AdapterName) { $a += " -AdapterName `"$AdapterName`"" }
    if ($Mac) { $a += " -Mac `"$Mac`"" }
    if ($Spoof) { $a += ' -Spoof' }
    if ($Restore) { $a += ' -Restore' }
    if ($Revive) { $a += ' -Revive' }
    if ($RandomizeHostname) { $a += ' -RandomizeHostname' }
    Start-Process powershell.exe -ArgumentList $a -Verb RunAs
    exit
}
if (-not (Test-IsAdmin)) {
    Write-Host '[ERROR] Administrator privileges required.' -ForegroundColor Red
    exit 1
}

if (-not $Headless) {
    Clear-Host
    Write-Host '==========================================================' -ForegroundColor Cyan
    Write-Host '              MAC Identity Spoofer              ' -ForegroundColor White
    Write-Host '==========================================================' -ForegroundColor Cyan
    Write-Host ''
}

$adapterList = Get-Adapters
if (-not $adapterList -or $adapterList.Count -eq 0) {
    Write-Host '[ERROR] No compatible network adapters detected.' -ForegroundColor Red
    if (-not $Headless) { $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown') }
    exit 1
}

if (-not $AdapterName) {
    if ($Headless) {
        Write-Host '[ERROR] -AdapterName is required in headless mode.' -ForegroundColor Red
        exit 1
    }
    Write-Host 'Available network adapters:' -ForegroundColor Gray
    for ($i = 0; $i -lt $adapterList.Count; $i++) {
        $a = $adapterList[$i]
        $color = if ($a.IsDisabled) { 'DarkYellow' } elseif ($a.RawStatus -eq 'Up') { 'Green' } else { 'Gray' }
        Write-Host (" [{0}] {1,-12} | {2,-38} | Status: {3} | MAC: {4}" -f ($i+1), $a.Name, $a.InterfaceDescription, $a.Status, $a.MacAddress) -ForegroundColor $color
    }
    Write-Host ''
    $choice = Read-Host "Select adapter [1-$($adapterList.Count)]"
    if ($choice -match '^\d+$' -and [int]$choice -ge 1 -and [int]$choice -le $adapterList.Count) {
        $selected = $adapterList[[int]$choice - 1]
    } else {
        $selected = $adapterList | Where-Object { $_.RawStatus -eq 'Up' } | Select-Object -First 1
        if (-not $selected) { $selected = $adapterList[0] }
    }
} else {
    $selected = $adapterList | Where-Object { $_.Name -eq $AdapterName } | Select-Object -First 1
}

if (-not $selected) {
    Write-Host "[ERROR] Adapter '$AdapterName' was not found." -ForegroundColor Red
    if (-not $Headless) { $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown') }
    exit 1
}

if (-not $Headless -and -not $Spoof -and -not $Restore -and -not $Revive) {
    Write-Host "`nTarget: $($selected.Name)  [$($selected.InterfaceDescription)]" -ForegroundColor Cyan
    Write-Host 'What action would you like to perform?' -ForegroundColor Yellow
    Write-Host ' [1] Spoof with a new random MAC & apply changes'
    Write-Host ' [2] Restore original factory hardware MAC'
    Write-Host ' [3] Full Stealth: Spoof MAC + Randomize PC Hostname'
    Write-Host ' [4] Revive / Re-enable hardware adapter'
    Write-Host ' [Q] Quit'
    $action = Read-Host "`nEnter choice [1/2/3/4/Q]"
    switch ($action) {
        '2' { $Restore = $true }
        '3' { $RandomizeHostname = $true }
        '4' { $Revive = $true }
        'Q' { exit }
        'q' { exit }
        default { }
    }
}
if ($Headless -and -not $Spoof -and -not $Restore -and -not $Revive) {
    Write-Host '[ERROR] No action specified.' -ForegroundColor Red
    exit 1
}

$regKey = Get-RegistryAdapterPath -Guid $selected.InterfaceGuid

if ($Revive) {
    Write-Host "`n[*] Reviving hardware adapter '$($selected.Name)'..." -ForegroundColor Yellow
    if ($selected.PnpDeviceID) {
        Enable-PnpDevice -InstanceId $selected.PnpDeviceID -Confirm:$false -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
    }
    Enable-NetAdapter -Name $selected.Name -Confirm:$false -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    $check = Get-NetAdapter -Name $selected.Name -ErrorAction SilentlyContinue
    Write-Host "[SUCCESS] Adapter revived. Status: $($check.Status), MAC: $($check.MacAddress)" -ForegroundColor Green

} elseif ($Restore) {
    Write-Host "`n[*] Restoring factory hardware MAC..." -ForegroundColor Yellow
    $prop = Get-NetAdapterAdvancedProperty -Name $selected.Name -ErrorAction SilentlyContinue | Where-Object { $_.RegistryKeyword -eq 'NetworkAddress' }
    if ($prop -and $prop.DisplayName) {
        Reset-NetAdapterAdvancedProperty -Name $selected.Name -DisplayName $prop.DisplayName -Confirm:$false -ErrorAction SilentlyContinue
    }
    if ($regKey) { Remove-ItemProperty -Path $regKey -Name 'NetworkAddress' -ErrorAction SilentlyContinue }
    Restart-AdapterSafe -Name $selected.Name -PnpId $selected.PnpDeviceID
    Update-DhcpLease -Name $selected.Name
    $verify = Get-NetAdapter -Name $selected.Name -ErrorAction SilentlyContinue
    Write-Host "`n[SUCCESS] Factory MAC restored: $($verify.MacAddress)" -ForegroundColor Green

} else {
    if ($Mac) {
        if (-not (Test-MacString $Mac)) {
            Write-Host "[ERROR] Invalid MAC: '$Mac'. Use 12 hex digits or AA-BB-CC-DD-EE-FF." -ForegroundColor Red
            exit 1
        }
        $newRawMac = Normalize-Mac $Mac
        $firstByte = [Convert]::ToInt32($newRawMac.Substring(0,2),16)
        if (($firstByte -band 1) -ne 0) { Write-Host '[!] Custom MAC has the multicast bit set - some networks will reject it.' -ForegroundColor Yellow }
        if (($firstByte -band 2) -eq 0) { Write-Host '[!] Custom MAC is not locally administered - driver may ignore it.' -ForegroundColor Yellow }
    } else {
        $newRawMac = New-ValidLAA-Mac
    }
    $newFormattedMac = ($newRawMac -split '(..)' | Where-Object { $_ }) -join '-'

    Write-Host "`n[*] Generated valid Unicast LAA MAC: $newFormattedMac" -ForegroundColor Green
    Write-Host '[*] Applying to network driver registry...' -ForegroundColor Cyan

    Set-NetAdapterAdvancedProperty -Name $selected.Name -RegistryKeyword 'NetworkAddress' -RegistryValue $newRawMac -Confirm:$false -ErrorAction SilentlyContinue
    if ($regKey) { Set-ItemProperty -Path $regKey -Name 'NetworkAddress' -Value $newRawMac -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 500

    Restart-AdapterSafe -Name $selected.Name -PnpId $selected.PnpDeviceID
    Update-DhcpLease -Name $selected.Name

    $verify = Get-NetAdapter -Name $selected.Name -ErrorAction SilentlyContinue
    $actual = if ($verify) { ($verify.MacAddress -replace '[^0-9A-Fa-f]','').ToUpperInvariant() } else { '' }
    $regVal = ''
    if ($regKey) { $regVal = ([string](Get-ItemProperty -Path $regKey -Name 'NetworkAddress' -ErrorAction SilentlyContinue).NetworkAddress) -replace '[^0-9A-Fa-f]','' }
    $matches = ($actual -eq $newRawMac) -or ($regVal -ieq $newRawMac)

    Write-Host ''
    if ($matches) {
        Write-Host '==========================================================' -ForegroundColor Green
        Write-Host ' [SUCCESS] Adapter MAC successfully updated!' -ForegroundColor Green
        Write-Host " Active MAC address : $($verify.MacAddress)" -ForegroundColor White
        Write-Host ' The router now sees an entirely new physical device.' -ForegroundColor Green
        Write-Host '==========================================================' -ForegroundColor Green
    } else {
        Write-Host "[!] Adapter reports MAC: $($verify.MacAddress)" -ForegroundColor Yellow
        if ($selected.MediaType -match '802\.11|Wireless|Wi-Fi') {
            Write-Host '    This wireless driver may ignore NetworkAddress. Check the adapter' -ForegroundColor Yellow
            Write-Host '    advanced properties for a MAC / Locally Administered Address field.' -ForegroundColor Yellow
        } else {
            Write-Host '    The new MAC activates after a reconnect or driver reload.' -ForegroundColor Gray
        }
    }

    if ($RandomizeHostname) {
        $randHex = -join ((48..57) + (65..90) | Get-Random -Count 7 | ForEach-Object { [char]$_ })
        $newHost = "DESKTOP-$randHex"
        Write-Host "`n[*] Setting new computer hostname to: $newHost" -ForegroundColor Magenta
        Rename-Computer -NewName $newHost -Force -ErrorAction SilentlyContinue
        Write-Host '[!] Hostname takes full network effect after the next reboot.' -ForegroundColor Yellow
    }
}

if (-not $Headless) {
    Write-Host ''
    Write-Host 'Press any key to close...' -ForegroundColor Gray
    $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
}
