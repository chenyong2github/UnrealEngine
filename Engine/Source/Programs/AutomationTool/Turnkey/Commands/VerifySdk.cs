// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using Gauntlet;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace Turnkey.Commands
{
	class VerifySdk : TurnkeyCommand
	{
		protected override void Execute(string[] CommandOptions)
		{
			bool bUnattended = TurnkeyUtils.ParseParam("Unattended", CommandOptions);
			bool bPreferFullSdk = TurnkeyUtils.ParseParam("PreferFull", CommandOptions);
			bool bForceSdkInstall = TurnkeyUtils.ParseParam("ForceSdkInstall", CommandOptions);
			bool bForceDeviceInstall = TurnkeyUtils.ParseParam("ForceDeviceInstall", CommandOptions);
			bool bUpdateIfNeeded = bForceSdkInstall || bForceDeviceInstall || TurnkeyUtils.ParseParam("UpdateIfNeeded", CommandOptions);

			// track each platform to check, and 
			Dictionary<UnrealTargetPlatform, List<string>> PlatformsAndDevices = null;

			// look at any devices on the commandline, and see if they have platforms or not
			string DeviceList = TurnkeyUtils.ParseParamValue("Device", null, CommandOptions);
			List<string> SplitDeviceList = null;
			if (DeviceList != null)
			{
				SplitDeviceList = DeviceList.Split("+".ToCharArray()).ToList();

				// look if they have platform@ tags
				bool bAnyHavePlatform = SplitDeviceList.Any(x => x.Contains("@"));
				if (bAnyHavePlatform)
				{
					if (!SplitDeviceList.All(x => x.Contains("@")))
					{
						throw new AutomationException("If any device in -device has a platform indicator ('Platform@Device'), they must all have a platform indicator");
					}

					// now split it up for devices for each platform
					foreach (string DeviceToken in SplitDeviceList)
					{
						string[] Tokens = DeviceToken.Split("@".ToCharArray(), StringSplitOptions.RemoveEmptyEntries);
						if (Tokens.Length != 2)
						{
							throw new AutomationException("{0} did not have the Platform@Device format", DeviceToken);
						}
						UnrealTargetPlatform Platform;
						if (!UnrealTargetPlatform.TryParse(Tokens[0], out Platform))
						{
							TurnkeyUtils.Log("Platform indicator {0} is an invalid platform, skipping", Tokens[0]);
							continue;
						}

						string DeviceName = Tokens[1];

						// track it
						if (PlatformsAndDevices == null)
						{
							PlatformsAndDevices = new Dictionary<UnrealTargetPlatform, List<string>>();
						}
						if (!PlatformsAndDevices.ContainsKey(Platform))
						{
							PlatformsAndDevices[Platform] = new List<string>();
						}
						PlatformsAndDevices[Platform].Add(DeviceName);
					}
					SplitDeviceList = null;
				}
			}

			// if we didn't get some platforms already from -device list, then get or ask the user for platforms
			if (PlatformsAndDevices == null)
			{
				PlatformsAndDevices = new Dictionary<UnrealTargetPlatform, List<string>>();
				List<UnrealTargetPlatform> ChosenPlatforms = TurnkeyUtils.GetPlatformsFromCommandLineOrUser(CommandOptions, UnrealTargetPlatform.GetValidPlatforms().ToList());
				ChosenPlatforms.ForEach(x => PlatformsAndDevices.Add(x, null));

				if (ChosenPlatforms.Count > 1 && SplitDeviceList != null && !(SplitDeviceList.Count == 1 && SplitDeviceList[0].CompareTo("All") == 0))
				{
					throw new AutomationException("When passing -Device to VerifySdk without platform specifiers ('Platform:Device'), a single platform must be specified (unless -Device=All is used)");
				}
			}

			if (PlatformsAndDevices.Count == 0)
			{
				TurnkeyUtils.Log("Platform(s) needed for VerifySdk command. Ending command.");
				return;
			}


			TurnkeyUtils.Log("Installed Sdk validity:");
			TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Success;

			CopyProviderRetriever Retriever = new CopyProviderRetriever();

			// check all the platforms
			foreach (var Pair in PlatformsAndDevices)
			{
				UnrealTargetPlatform Platform = Pair.Key;
				UEBuildPlatformSDK PlatformSDK = UEBuildPlatformSDK.GetSDKForPlatform(Platform.ToString());

				// get the platform object
				AutomationTool.Platform AutomationPlatform = AutomationTool.Platform.GetPlatform(Platform);

				SdkUtils.LocalAvailability LocalState = SdkUtils.GetLocalAvailability(AutomationPlatform, bUpdateIfNeeded);

				string ManualSDKVersion, AutoSDKVersion;
				string MinAllowedVersion, MaxAllowedVersion;
				string MinSoftwareAllowedVersion, MaxSoftwareAllowedVersion;
				PlatformSDK.GetInstalledVersions(out ManualSDKVersion, out AutoSDKVersion);
				PlatformSDK.GetValidVersionRange(out MinAllowedVersion, out MaxAllowedVersion);
				PlatformSDK.GetValidSoftwareVersionRange(out MinSoftwareAllowedVersion, out MaxSoftwareAllowedVersion);

				SdkUtils.LocalAvailability ReportedState = LocalState;
				string StatusString;

				if ((LocalState & SdkUtils.LocalAvailability.Platform_ValidHostPrerequisites) == 0)
				{
					StatusString = "Invalid";
				}
				else if ((LocalState & (SdkUtils.LocalAvailability.AutoSdk_ValidVersionExists | SdkUtils.LocalAvailability.InstalledSdk_ValidVersionExists)) == 0)
				{
					StatusString = "Invalid";
					TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;
				}
				else
				{
					StatusString = "Valid";
					ReportedState &= (SdkUtils.LocalAvailability.AutoSdk_ValidVersionExists | SdkUtils.LocalAvailability.InstalledSdk_ValidVersionExists);
					//					TurnkeyUtils.Log("{0}: Valid [Installed: '{1}', Required: '{2}']", Platform, PlatformObject.GetInstalledSdk(), PlatformObject.GetAllowedSdks());
				}

				TurnkeyUtils.Report("{0}: (Status={1}, Installed={2}, AutoSDK={3}, MinAllowed={4}, MaxAllowed={5}, Flags=\"{6}\")", Platform, StatusString, ManualSDKVersion, AutoSDKVersion,
					MinAllowedVersion, MaxAllowedVersion, ReportedState.ToString());

				// install if out of date, or if forcing it
				if (bForceSdkInstall || (bUpdateIfNeeded && TurnkeyUtils.ExitCode != AutomationTool.ExitCode.Success))
				{
					TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Success;

// 						if (!bUnattended)
// 						{
// 							string Response = TurnkeyUtils.ReadInput("Your Sdk installation is not up to date. Would you like to install a valid Sdk? [Y/n]", "Y");
// 							if (string.Compare(Response, "Y", true) != 0)
// 							{
// 								continue;
// 							}
// 						}

					FileSource BestSdk = null;
					// find the best Sdk, prioritizing as request
					if (bPreferFullSdk)
					{
						BestSdk = FileSource.FindMatchingSdk(AutomationPlatform, new FileSource.SourceType[] { FileSource.SourceType.Full, FileSource.SourceType.BuildOnly, FileSource.SourceType.AutoSdk }, bSelectBest: bUnattended);
					}
					else
					{
						BestSdk = FileSource.FindMatchingSdk(AutomationPlatform, new FileSource.SourceType[] { FileSource.SourceType.AutoSdk, FileSource.SourceType.BuildOnly, FileSource.SourceType.Full }, bSelectBest: bUnattended);
					}

					if (BestSdk == null)
					{
						TurnkeyUtils.Log("ERROR: {0}: Unable to find any Sdks that could be installed", Platform);
						TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;
						continue;
					}

					TurnkeyUtils.Log("Will install {0}", BestSdk.Name);

					if (BestSdk.DownloadOrInstall(Platform) == false)
					{
						TurnkeyUtils.Log("Install failed!");
						continue;
					}
// 					AutomationPlatform.InstallSDK(TurnkeyUtils.CommandUtilHelper, Retriever, BestSdk);


					// update LocalState
//					LocalState = SdkUtils.GetLocalAvailability(AutomationPlatform, false);

					// @todo turnkey: validate!
				}

				// use the per-platform device list, unless it's not specifed, then use the global device list (which is set when not using platform specifiers)
				List<string> DeviceNames = Pair.Value != null ? Pair.Value : SplitDeviceList;
				if (DeviceNames != null && DeviceNames.Count > 0)
				{
					DeviceInfo[] Devices = AutomationPlatform.GetDevices();
					if (Devices == null)
					{
						TurnkeyUtils.Log("Platform {0} didn't have any devices, ignoring any devices specified", Platform);
						continue;
					}

					TurnkeyUtils.Log("Installed Device validity:");

					// a single device named all means all devices
					if (!(DeviceNames.Count == 1 && DeviceNames[0].ToLower() == "all"))
					{
						Devices = Devices.Where(x => DeviceNames.Contains(x.Name, StringComparer.OrdinalIgnoreCase)).ToArray();
					}

					// now check software verison of each device
					foreach (DeviceInfo Device in Devices)
					{
						bool bArePrerequisitesValid = AutomationPlatform.UpdateDevicePrerequisites(Device, TurnkeyUtils.CommandUtilHelper, Retriever, !bUpdateIfNeeded);
						bool bIsSoftwareValid = PlatformSDK.IsSoftwareVersionValid(Device.SoftwareVersion);

						SdkUtils.LocalAvailability DeviceState = SdkUtils.LocalAvailability.None;
						if (!bArePrerequisitesValid)
						{
							StatusString = "Invalid";
							DeviceState |= SdkUtils.LocalAvailability.Device_InvalidPrerequisites;
						}
						else if (bIsSoftwareValid)
						{
							StatusString = "Valid";
							DeviceState |= SdkUtils.LocalAvailability.Device_InstallSoftwareValid;
						}
						else
						{
							StatusString = "Invalid";
							DeviceState |= SdkUtils.LocalAvailability.Device_InstallSoftwareInvalid;
						}

						if (Device.bCanConnect== false)
						{
							DeviceState |= SdkUtils.LocalAvailability.Device_CannotConnect;
						}

						TurnkeyUtils.Report("{0}@{1}: (Status={2}, Installed={3}, MinAllowed={4}, MaxAllowed={5}, Flags=\"{6}\")", Platform, Device.Name, StatusString, Device.SoftwareVersion, 
							MinSoftwareAllowedVersion, MaxSoftwareAllowedVersion, DeviceState.ToString());

						if (bForceDeviceInstall || !bIsSoftwareValid)
						{
							TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;

							if (bUpdateIfNeeded)
							{
								FileSource MatchingInstallableSdk = FileSource.FindMatchingSdk(AutomationPlatform, new FileSource.SourceType[] { FileSource.SourceType.Flash }, bSelectBest: bUnattended, DeviceType: Device.Type);

								if (MatchingInstallableSdk == null)
								{
									TurnkeyUtils.Log("ERROR: {0}: Unable top find any Sdks that could be installed on {1}", Platform, Device.Name);
									TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;
								}
								else
								{
									MatchingInstallableSdk.DownloadOrInstall(Platform, Device, bUnattended);
									TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Success;
								}
							}
						}
					}
				}
			}
		}
	}
}
