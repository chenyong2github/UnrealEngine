# Copyright Epic Games, Inc. All Rights Reserved.

# Unclear if we need this?
# Set-ExecutionPolicy Bypass -Scope Process -Force

# Versions are from current working release versions
#
# Structure for installation preparation; please note | in "how to install" -> installer will split the command
#       Need install Package name   Version   min/any      how to get version  how to install           path to be added
$Packages = @(@("y", "chocolatey",  "0.11.3", "min",       "choco --version",  "[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072 | Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1')) | choco upgrade chocolatey"),
			  @("y", "node",        "v17.0.1","min",       "node --version",   "choco install nodejs -y -x -f"),
			  @("y", "npm",         "8.1.2",  "min",       "npm --version",    "npm install -g npm -f")
			  )

# Install npm packages at the correct place
Push-Location $PSScriptRoot\..\..\

# Check what to install
foreach ($Item in $Packages) {
 Write-Host "Checking for " $Item[1].padRight(12) " ..." -NoNewLine
 if ($Item[3] -eq "any") {
  Write-Host " any version ...                " -NoNewLine
  $IsInstalled = Get-Command $Item[4] -ErrorAction SilentlyContinue
  if ($IsInstalled -eq $null) {
	Write-Host " not found                      marked for installation"
  } else {
	Write-Host " found                          no install needed"
	$Item[0] = "n"
  }
 } elseif ($item[3] -eq "min") {
  Write-Host " minimum version: " $Item[2].padRight(12) -NoNewLine
  $Wanted = $Item[2] -replace "[^0-9.]"
  $Installed = Invoke-Expression -Command $Item[4] 2>&1
  if ($Installed -eq $null) {
   Write-Host "  not found an installed version" $Item[4]
  } else {
   Write-Host "  found version: " $Installed.padRight(15) -NoNewLine
   $Current = $Installed -replace "[^0-9.]"
   if ([System.Version]$Current -lt [System.Version]$Wanted) {
    Write-Host "old marked for installation"
   } else {
    $item[0] = "n"
    Write-Host "no install needed"
   }
  }
 } else {
  Write-Host "Code error, please check Packages setup for " $Item[1]
  exit
 }
}

# Do the installation
foreach ($Item in $Packages) {
 if ($Item[0] -eq "n") {
  continue;
 }
 if ($Item[5].Substring(0, 1) -eq ":") {
  Write-Host "Will not install " $Item[1] " because " $Item[5]
 } else {
  $HasPipe = $Item[5].indexOf("|")
  if ($HasPipe) {
   $Arr = $Item[5].Split("|");
   foreach($InstallExe in $Arr) {
    $InstallExe = $InstallExe.trim()
    Invoke-Expression $InstallExe
   }
  } else {
   Invoke-Expression -Command $Item[5]
  }
  $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
 }
}

# Install Cirrus
npm install

# Reverse ..\.. location
Pop-Location

# Put us in the cmd scripts folder
Push-Location $PSScriptRoot

# Install CoTURN
Write-Host "Checking for  Coturn..." -NoNewLine
if (-not(Test-Path -Path coturn/turnserver.exe -PathType Leaf)) {
    Write-Host " ...installing... " -NoNewLine
    curl -o ./turnserver.zip https://github.com/mcottontensor/coturn/releases/download/v4.5.2-windows/turnserver.zip
    Expand-Archive -Path ./turnserver.zip -DestinationPath ./coturn
    Remove-Item -Path ./turnserver.zip
    Write-Host " ...done. "
}
else {
    Write-Host " ...found."
}

# Reverse location
Pop-Location
