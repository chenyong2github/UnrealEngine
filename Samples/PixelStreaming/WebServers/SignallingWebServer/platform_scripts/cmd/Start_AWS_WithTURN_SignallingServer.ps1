# Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
Param (
    $TurnServer,
    $StunServer = "stun.l.google.com:19302"
)

Push-Location $PSScriptRoot

& "$PSScriptRoot\Start_AWS_TURNServer.ps1"

$PublicIp = Invoke-WebRequest -Uri "http://169.254.169.254/latest/meta-data/public-ipv4" -UseBasicParsing

if(!$PublicIP){
    $PublicIP = "127.0.0.1"
}

if(!$PSBoundParameters.ContainsKey("TurnServer")) {
    $TurnServer = "" + $PublicIp + ":19303"
}

Write-Output "Public IP: $PublicIp"

$peerConnectionOptions = "{ \""iceServers\"": [{\""urls\"": [\""stun:" + $StunServer + "\"",\""turn:" + $TurnServer + "\""], \""username\"": \""PixelStreamingUser\"", \""credential\"": \""AnotherTURNintheroad\""}] }"

$ProcessExe = "node.exe"
$Arguments = @("cirrus", "--peerConnectionOptions=""$peerConnectionOptions""", "--publicIp=$PublicIp")
# Add arguments passed to script to Arguments for executable
$Arguments += $args

Push-Location ..\..
Write-Output "Running: $ProcessExe $Arguments"
Start-Process -FilePath $ProcessExe -ArgumentList $Arguments -Wait -NoNewWindow
Pop-Location

Pop-Location