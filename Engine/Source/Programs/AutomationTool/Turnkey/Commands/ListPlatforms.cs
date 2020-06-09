// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using UnrealBuildTool;
using AutomationTool;


namespace Turnkey.Commands
{
	class ListPlatforms : TurnkeyCommand
	{
		protected override void Execute(string[] CommandOptions)
		{
			TurnkeyUtils.Log("");
			TurnkeyUtils.Log("Valid Platforms:");
			foreach (UnrealTargetPlatform TargetPlatform in UnrealTargetPlatform.GetValidPlatforms())
			{
				Platform Platform = Platform.Platforms[new TargetPlatformDescriptor(TargetPlatform)];

				string InstalledSdk = Platform.GetInstalledSdk();
				string AllowedSdks = Platform.GetAllowedSdks();
				string AllowedSoftware = Platform.GetAllowedSoftwareVersions();
				bool bIsSdkValid = TurnkeyUtils.IsValueValid(InstalledSdk, AllowedSdks, Platform);
				TurnkeyUtils.Log("  Platform: {0}", TargetPlatform.ToString());
				TurnkeyUtils.Log("  Installed Sdk: {0}", InstalledSdk);
				TurnkeyUtils.Log("  Allowed Sdk(s): {0}", AllowedSdks);
				TurnkeyUtils.Log("  Valid SDK Installed? {0}", bIsSdkValid);

				// look for available sdks
				List<SdkInfo> MatchingFullSdks = TurnkeyManifest.GetDiscoveredSdks()?.FindAll(x => x.Type == SdkInfo.SdkType.Full && x.IsValid(TargetPlatform));
				if (MatchingFullSdks == null || MatchingFullSdks.Count == 0)
				{
					TurnkeyUtils.Log("    NO MATCHING FULL SDK FOUND!");
				}
				else
				{
					TurnkeyUtils.Log("    Possible Full Sdks that could be installed:");

					foreach (SdkInfo Sdk in MatchingFullSdks)
					{
						TurnkeyUtils.Log(Sdk.ToString(4));
					}
				}

				TurnkeyUtils.Log("  Allowed Device Software Version(s): {0}", AllowedSoftware);
				TurnkeyUtils.Log("  Devices: ");

				DeviceInfo[] Devices = Platform.GetDevices();
				if (Devices == null || Devices.Length == 0)
				{
					TurnkeyUtils.Log("    NO DEVICES FOUND!");
				}
				else
				{
					foreach (DeviceInfo Device in Devices)
					{
						bool bIsSoftwareValid = TurnkeyUtils.IsValueValid(Device.SoftwareVersion, AllowedSoftware, Platform);

						TurnkeyUtils.Log("    Name: {0}{1}", Device.Name, Device.bIsDefault ? "*" : "");
						TurnkeyUtils.Log("      Id: {0}", Device.Id);
						TurnkeyUtils.Log("      Type: {0}", Device.Type);
						TurnkeyUtils.Log("      Installed Software Version: {0}", Device.SoftwareVersion);
						TurnkeyUtils.Log("      Valid Software Installed?: {0}", bIsSoftwareValid);

						if (!bIsSoftwareValid)
						{
							// look for available flash
							List<SdkInfo> MatchingFlashSdks = TurnkeyManifest.GetDiscoveredSdks()?.FindAll(x => x.Type == SdkInfo.SdkType.Flash && x.IsValid(TargetPlatform, Device.Name));
							if (MatchingFlashSdks == null || MatchingFlashSdks.Count == 0)
							{
								TurnkeyUtils.Log("      NO MATCHING FLASH SDK FOUND!");
							}
							else
							{
								TurnkeyUtils.Log("      Possible Flash Sdks that could be installed:");

								foreach (SdkInfo Sdk in MatchingFlashSdks)
								{
									TurnkeyUtils.Log(Sdk.ToString(6));
								}
							}
						}
					}
				}
				TurnkeyUtils.Log("");
			}

		}
	}
}
