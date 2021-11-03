# Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
Param (
    $StunServer = "stun.l.google.com:19302"
)
$PublicIp = Invoke-WebRequest -Uri "http://169.254.169.254/latest/meta-data/public-ipv4" -UseBasicParsing

Write-Output "Public IP: $PublicIp"

$peerConnectionOptions = "{ \""iceServers\"": [{\""urls\"": [\""stun:" + $StunServer + "\""]}] }"

$ProcessExe = "node.exe"
$Arguments = @("cirrus", "--peerConnectionOptions=""$peerConnectionOptions""", "--publicIp=$PublicIp")
# Add arguments passed to script to Arguments for executable
$Arguments += $args

Push-Location ..\..
Write-Output "Running: $ProcessExe $Arguments"
Start-Process -FilePath $ProcessExe -ArgumentList $Arguments -Wait -NoNewWindow
Pop-Location
