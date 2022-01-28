# Copyright Epic Games, Inc. All Rights Reserved.
Push-Location $PSScriptRoot\..\

$PublicIP = Invoke-WebRequest -Uri "https://api.ipify.org" -UseBasicParsing
$Arguments = "--PublicIP=$PublicIP"

Write-Output "Arguments: $Arguments"

npm start -- $Arguments

Pop-Location