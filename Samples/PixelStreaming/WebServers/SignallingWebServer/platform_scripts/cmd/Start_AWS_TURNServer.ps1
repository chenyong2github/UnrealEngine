Push-Location $PSScriptRoot

& "$PSScriptRoot\Install_CoTURN.ps1"

$LocalIp = Invoke-WebRequest -Uri "http://169.254.169.254/latest/meta-data/local-ipv4"
$PublicIp = Invoke-WebRequest -Uri "http://169.254.169.254/latest/meta-data/public-ipv4"
Write-Output "Private IP: $LocalIp"
Write-Output "Public IP: $PublicIp"

$TurnUsername = "PixelStreamingUser"
$TurnPassword = "AnotherTURNintheroad"
$Realm = "PixelStreaming"
$ProcessExe = "turnserver.exe"
$Arguments = "-p 19303 -r $Realm -X $PublicIP -E $LocalIP -L $LocalIP --no-cli --no-tls --no-dtls --pidfile `"C:\coturn.pid`" -a -v -n -u $TurnUsername`:$TurnPassword"

# Add arguments passed to script to Arguments for executable
$Arguments += $args

Push-Location ..\..
Write-Output "Running: $ProcessExe $Arguments"
# pause
Start-Process -FilePath $ProcessExe -ArgumentList $Arguments
Pop-Location

Pop-Location