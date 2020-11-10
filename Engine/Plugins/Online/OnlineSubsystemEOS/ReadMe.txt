This plugin is experimental and meant to be something to jump start your development. This plugin has been tested using ShooterGame on Windows

It does not meet the Epic Games Store requirement for cross store support as is. You will need to add support to ship on EGS.

Upgrading EOS SDK versions
	1. Download a new SDK version to the EOS directory
	2. Unzip it to a directory named EOS-SDK-<version>
	3. Update Source/OnlineSubsystemEOS.Build.cs to the new version by changing the line below
			string EOSSDKVersion = "EOS-SDK-<version num>";
	4. On Windows, copy the DLL(s) that you are using to the Engine/Binaries directory (they must be in the same dir as the exe)
	5. Regenerate your UE4 projects
	6. Clean (required because the define must be regenerated) & Build

This plugin supports:
	- EAS/EGS Auth
	- EAS Presence
	- EAS Friends
	- EGS/EAS Ecom
	- EOS Sessions
	- EOS Stats
	- EOS Achievements
	- EOS Leaderboards
	- EOS p2p Sockets
	- EOS Metrics (session based analytics)

To configure your settings:
	1. Go to Project Settings
	2. Scroll down to Plugins
	3. Select Epic Online Services (see EOS Plugin Settings.jpg)
	4. Add a entry in the list for each artifact on EGS or use the default name for non-EGS
		- Each deployment id/artifact can have separate settings
