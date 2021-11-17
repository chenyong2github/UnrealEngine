$TempFile = New-TemporaryFile

& "dotnet" ".\Shared\OidcToken\OidcToken.dll" "--Service=EpicGames-Okta-Jupiter" "--OutFile" $TempFile.FullName 
Get-Content -Raw -Path $TempFile.FullName | ConvertFrom-Json | ForEach-Object { "Authorization: Bearer $($_.Token)" } | Out-File .\curl_auth_header