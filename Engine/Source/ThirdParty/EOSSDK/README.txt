This module exists to house the EOSSDK, which is then managed by the EOSShared plugin.

Installing / Upgrading the SDK.
	1. Download the SDK, and create a new folder under Engine/Source/ThirdParty/EOSSDK/ e.g. EOSSDK1.12 to house the SDK.
	2. Unzip the SDK to the new folder.
	3. In EOSSDK.Build.cs, ensure the "bBuildWithEOS" flag is set to true, and update the SDKBaseDir to point at the module relative SDK directory e.g. EOSSDK1.12/SDK
	4. Regenerate your project files.
	5. Build!