// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using Gauntlet;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EpicGames.Core;
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

			// track each platform to check, and if -device was specified, get the devices
			string DeviceString = TurnkeyUtils.ParseParamValue("Device", null, CommandOptions);
			List<UnrealTargetPlatform> PlatformsToCheck = new List<UnrealTargetPlatform>();
			List<DeviceInfo> DevicesToCheck = new List<DeviceInfo>();
			if (string.IsNullOrEmpty(DeviceString))
			{
				PlatformsToCheck = TurnkeyUtils.GetPlatformsFromCommandLineOrUser(CommandOptions, null);
			}
			else
			{
				DevicesToCheck = TurnkeyUtils.GetDevicesFromCommandLineOrUser(CommandOptions, null);
				// if we failed to get any devices, fallback to getting just platforms anyway
				if (DevicesToCheck == null)
				{
					if (TurnkeyUtils.ParseParamValue("Platform", null, CommandOptions) == null)
					{
						TurnkeyUtils.Log("Devices were requested, but none of them were found. Since -platforms was not specified, exiting command...");
						return;
					}
					TurnkeyUtils.Log("Devices were requested, but none of them were found. Will continue with just platform verification");
					PlatformsToCheck = TurnkeyUtils.GetPlatformsFromCommandLineOrUser(CommandOptions, null);
				}
				else
				{
					PlatformsToCheck = DevicesToCheck.Select(x => x.Platform).ToHashSet().ToList();
				}
			}

			if (PlatformsToCheck == null || PlatformsToCheck.Count == 0)
			{
				TurnkeyUtils.Log("Platform(s) and/or device(s) needed for VerifySdk command. Check parameters or selections.");
				return;
			}


			TurnkeyUtils.Log("Installed Sdk validity:");
			TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Success;

			CopyProviderRetriever Retriever = new CopyProviderRetriever();

			TurnkeyUtils.StartTrackingExternalEnvVarChanges();

			// check all the platforms
			foreach (UnrealTargetPlatform Platform in PlatformsToCheck)
			{
				UEBuildPlatformSDK PlatformSDK = UEBuildPlatformSDK.GetSDKForPlatform(Platform.ToString());

				// get the platform object
				AutomationTool.Platform AutomationPlatform = AutomationTool.Platform.GetPlatform(Platform);

				// checking availability may generate errors, if prerequisites are failing
				SdkUtils.LocalAvailability LocalState = SdkUtils.GetLocalAvailability(AutomationPlatform, bUpdateIfNeeded);

				string ManualSDKVersion = "", AutoSDKVersion = "";
				string MinAllowedVersion = "", MaxAllowedVersion = "";
				string MinSoftwareAllowedVersion = "", MaxSoftwareAllowedVersion = "";
				if (PlatformSDK != null)
				{
					PlatformSDK.GetInstalledVersions(out ManualSDKVersion, out AutoSDKVersion);
					PlatformSDK.GetValidVersionRange(out MinAllowedVersion, out MaxAllowedVersion);
					PlatformSDK.GetValidSoftwareVersionRange(out MinSoftwareAllowedVersion, out MaxSoftwareAllowedVersion);
				}

				SdkUtils.LocalAvailability ReportedState = LocalState;
				string StatusString;
				bool bHasOutOfDateSDK = false;

				if ((LocalState & SdkUtils.LocalAvailability.Platform_ValidHostPrerequisites) == 0)
				{
					StatusString = "Invalid";
				}
				else if ((LocalState & (SdkUtils.LocalAvailability.AutoSdk_ValidVersionExists | SdkUtils.LocalAvailability.InstalledSdk_ValidVersionExists)) == 0)
				{
					StatusString = "Invalid";
					bHasOutOfDateSDK = true;
				}
				else
				{
					StatusString = "Valid";
					ReportedState &= (SdkUtils.LocalAvailability.AutoSdk_ValidVersionExists | SdkUtils.LocalAvailability.InstalledSdk_ValidVersionExists | SdkUtils.LocalAvailability.Support_FullSdk | SdkUtils.LocalAvailability.Support_AutoSdk);
				}

				TurnkeyUtils.Report("{0}: (Status={1}, Installed={2}, AutoSDK={3}, MinAllowed={4}, MaxAllowed={5}, Flags=\"{6}\")", Platform, StatusString, ManualSDKVersion, AutoSDKVersion,
					MinAllowedVersion, MaxAllowedVersion, ReportedState.ToString());

				if (PlatformSDK == null)
				{
					continue;
				}

				// install if out of date, or if forcing it
				if (bForceSdkInstall || (bUpdateIfNeeded && bHasOutOfDateSDK))
				{
// 						if (!bUnattended)
// 						{
// 							string Response = TurnkeyUtils.ReadInput("Your Sdk installation is not up to date. Would you like to install a valid Sdk? [Y/n]", "Y");
// 							if (string.Compare(Response, "Y", true) != 0)
// 							{
// 								continue;
// 							}
// 						}

					if (AutomationPlatform.ShouldPerformManualSDKInstall())
					{
						if (AutomationPlatform.ManualInstallSDK(TurnkeyUtils.CommandUtilHelper, new CopyProviderRetriever(), null) == false)
						{
							TurnkeyUtils.Log("Install failed!");
						}
						continue;
					}

					// if the platform has a valid sdk but isn't the "installed one", then try to switch to it
					if ((LocalState & SdkUtils.LocalAvailability.InstalledSdk_ValidInactiveVersionExists) != 0)
					{

						// find the highest number that is valid (because a valid version exists, we know there will be at least one valid version)
						string BestAlternateVersion = PlatformSDK.GetAllInstalledSDKVersions().ToList().OrderByDescending(x => x).Where(x => PlatformSDK.IsVersionValid(x, false)).First();

						bool bWasSwitched = PlatformSDK.SwitchToAlternateSDK(BestAlternateVersion, false);

						if (bWasSwitched == true)
						{
							TurnkeyUtils.Log("Fast-switched to already-installed version {0}", BestAlternateVersion);

							// if SwitchToAlternateSDK returns true, then we are good to go!
							continue;
						}
					}

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

				// now check software verison of each device
				if (DevicesToCheck != null)
				{
					foreach (DeviceInfo Device in DevicesToCheck.Where(x => x.Platform == Platform))
					{
						List<string> DeviceErrorMessages = new List<string>();
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

						if (Device.bCanConnect == false)
						{
							DeviceState |= SdkUtils.LocalAvailability.Device_CannotConnect;
						}

						// @todo turnkey: if Name has a comma in it, we are in trouble, and maybe a )
						TurnkeyUtils.Report("{0}@{1}: (Name={2}, Status={3}, Installed={4}, MinAllowed={5}, MaxAllowed={6}, Flags=\"{7}\")", Platform, Device.Id, Device.Name, StatusString, Device.SoftwareVersion,
							MinSoftwareAllowedVersion, MaxSoftwareAllowedVersion, DeviceState.ToString());

						if (bForceDeviceInstall || !bIsSoftwareValid)
						{
							if (bUpdateIfNeeded)
							{
								if (Device.bCanConnect)
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
									}
								}
								else
								{
									TurnkeyUtils.Log("Skipping device {0} because it cannot connect.", Device.Name);
								}
							}
						}
					}
				}
			}

			TurnkeyUtils.EndTrackingExternalEnvVarChanges();
		}
	}
}
