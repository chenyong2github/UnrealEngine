$destFolder=".\Windows"
if (-not (Test-Path -Path $destFolder -PathType Container))
{
    New-Item -Path $destFolder -ItemType "directory"
}

# Run a separate PowerShell process because the script calls exit, so it will end the current PowerShell session.
&powershell -NoProfile -ExecutionPolicy unrestricted -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; &([scriptblock]::Create((Invoke-WebRequest -UseBasicParsing 'https://dot.net/v1/dotnet-install.ps1'))) -NoPath -Channel 3.1 -InstallDir $destFolder"