# Our supported command-line parameters
param (
	[string]$release = ""
)

# Determine if a specific Windows release was specified or if we are auto-detecting the host OS release
if ($release -ne "")
{
	# Use Hyper-V isolation mode to ensure compatibility with the specified Windows release
	$windowsRelease = $release
	$isolation = "hyperv"
	
	# Append the release to the image tag
	$tag = "runtime-windows-$release"
}
else
{
	# Retrieve the Windows release number (e.g. 1903, 1909, 2004, 20H2, etc.)
	$displayVersion = (Get-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -Name DisplayVersion -ErrorAction SilentlyContinue)
	if ($displayVersion) {
		$windowsRelease = $displayVersion.DisplayVersion
	} else {
		$windowsRelease = (Get-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -Name ReleaseId).ReleaseId
	}
	
	# Use process isolation mode for improved performance
	$isolation = "process"
	
	# Don't suffix the image tag
	$tag = "runtime-windows"
}

# Build our runtime container image using the correct base image for the selected Windows version
"Building runtime container image for Windows version $windowsRelease with ``$isolation`` isolation mode..."
docker build -t "ghcr.io/epicgames/unreal-engine:$tag" --isolation="$isolation" --build-arg "BASETAG=$windowsRelease" ./runtime
