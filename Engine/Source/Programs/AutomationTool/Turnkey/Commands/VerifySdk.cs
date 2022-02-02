// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using Gauntlet;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EpicGames.Core;
using UnrealBuildTool;
using UnrealBuildBase;

namespace Turnkey.Commands
{
	class VerifySdk : TurnkeyCommand
	{
		protected override CommandGroup Group => CommandGroup.Sdk;

		protected override void Execute(string[] CommandOptions)
		{
			bool bUnattended = TurnkeyUtils.ParseParam("Unattended", CommandOptions);
			bool bPreferFullSdk = TurnkeyUtils.ParseParam("PreferFull", CommandOptions);
			bool bForceSdkInstall = TurnkeyUtils.ParseParam("ForceSdkInstall", CommandOptions);
			bool bForceDeviceInstall = TurnkeyUtils.ParseParam("ForceDeviceInstall", CommandOptions);
			bool bUpdateIfNeeded = bForceSdkInstall || bForceDeviceInstall || TurnkeyUtils.ParseParam("UpdateIfNeeded", CommandOptions);
			bool bSkipPlatformCheck = TurnkeyUtils.ParseParam("SkipPlatform", CommandOptions);
			bool bAutoChooseBest = bUnattended || bUpdateIfNeeded;

			List<UnrealTargetPlatform> PlatformsToCheck;
			List<DeviceInfo> DevicesToCheck;
			TurnkeyUtils.GetPlatformsAndDevicesFromCommandLineOrUser(CommandOptions, true, out PlatformsToCheck, out DevicesToCheck);

			// if we got no devices, requested some, and platforms were not specified, then we don't want to continue.
			// if -device and -platform were specified, and no devices found, we will still continue with the platforms
			if (DevicesToCheck.Count == 0 && TurnkeyUtils.ParseParamValue("Device", null, CommandOptions) != null && TurnkeyUtils.ParseParamValue("Platform", null, CommandOptions) == null)
			{
				TurnkeyUtils.Log("Devices were requested, but none of them were found. Since -platforms was not specified, exiting command...");
			}

			if (PlatformsToCheck.Count == 0 && !bSkipPlatformCheck)
			{
				TurnkeyUtils.Log("Platform(s) and/or device(s) needed for VerifySdk command. Check parameters or selections.");
				return;
			}

			TurnkeyUtils.Log("Installed Sdk validity:");
			TurnkeyUtils.ExitCode = ExitCode.Success;

			TurnkeyContextImpl TurnkeyContext = new TurnkeyContextImpl();

			TurnkeyUtils.StartTrackingExternalEnvVarChanges();

			// check all the platforms
			foreach (UnrealTargetPlatform Platform in PlatformsToCheck)
			{
				UEBuildPlatformSDK PlatformSDK = UEBuildPlatformSDK.GetSDKForPlatform(Platform.ToString());

				// get the platform object
				AutomationTool.Platform AutomationPlatform = AutomationTool.Platform.GetPlatform(Platform);

				// reset the errors for each device
				TurnkeyContext.ErrorMessages.Clear();

				// checking availability may generate errors, if prerequisites are failing
				SdkUtils.LocalAvailability LocalState = SdkUtils.GetLocalAvailability(AutomationPlatform, bUpdateIfNeeded, TurnkeyContext);

				string ManualSDKVersion = "", AutoSDKVersion = "";
				string MinAllowedVersion = "", MaxAllowedVersion = "";
				string MinSoftwareAllowedVersion = "", MaxSoftwareAllowedVersion = "";

				try
				{
					if (PlatformSDK != null)
					{
						PlatformSDK.GetInstalledVersions(out ManualSDKVersion, out AutoSDKVersion);
						PlatformSDK.GetValidVersionRange(out MinAllowedVersion, out MaxAllowedVersion);
						PlatformSDK.GetValidSoftwareVersionRange(out MinSoftwareAllowedVersion, out MaxSoftwareAllowedVersion);
					}
				}
				catch (Exception)
				{
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

				string ErrorString = "";
				if (TurnkeyContext.ErrorMessages.Count() > 0)
				{
					ErrorString = string.Format(", Error=\"{0}\"", string.Join("|", TurnkeyContext.ErrorMessages));
				}

				TurnkeyUtils.Report("{0}: (Status={1}, Installed={2}, AutoSDK={3}, MinAllowed={4}, MaxAllowed={5}, Flags=\"{6}\"{7})", Platform, StatusString, ManualSDKVersion, AutoSDKVersion,
					MinAllowedVersion, MaxAllowedVersion, ReportedState.ToString(), ErrorString);

				if (PlatformSDK == null)
				{
					continue;
				}

				// install if out of date, or if forcing it
				if (bForceSdkInstall || (bUpdateIfNeeded && bHasOutOfDateSDK))
				{
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
						BestSdk = FileSource.FindMatchingSdk(AutomationPlatform, new FileSource.SourceType[] { FileSource.SourceType.Full, FileSource.SourceType.BuildOnly, FileSource.SourceType.AutoSdk }, bSelectBest: bAutoChooseBest);
					}
					else
					{
						BestSdk = FileSource.FindMatchingSdk(AutomationPlatform, new FileSource.SourceType[] { FileSource.SourceType.AutoSdk, FileSource.SourceType.BuildOnly, FileSource.SourceType.Full }, bSelectBest: bAutoChooseBest);
					}

					if (BestSdk == null)
					{
						TurnkeyUtils.Log("ERROR: {0}: Unable to find any Sdks that could be installed", Platform);
						TurnkeyUtils.ExitCode = ExitCode.Error_SDKNotFound;
						continue;
					}

					TurnkeyUtils.Log("Will install {0}", BestSdk.Name);

					if (BestSdk.DownloadOrInstall(Platform, TurnkeyContext, null, bUnattended) == false)
					{
						TurnkeyUtils.Log("Failed to install {0}", BestSdk.Name);
						TurnkeyUtils.ExitCode = ExitCode.Error_SDKInstallFailed;
						continue;
					}

					// update LocalState
//					LocalState = SdkUtils.GetLocalAvailability(AutomationPlatform, false);

					// @todo turnkey: validate!
				}

				// now check software verison of each device
				if (DevicesToCheck != null)
				{
					foreach (DeviceInfo Device in DevicesToCheck.Where(x => x.Platform == Platform))
					{
						// reset the errors for each device
						TurnkeyContext.ErrorMessages.Clear();

						bool bArePrerequisitesValid = AutomationPlatform.UpdateDevicePrerequisites(Device, TurnkeyUtils.CommandUtilHelper, TurnkeyContext, !bUpdateIfNeeded);
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

						string DeviceErrorString = "";
						if (TurnkeyContext.ErrorMessages.Count() > 0)
						{
							ErrorString = string.Format(", Error=\"{0}\"", string.Join("|", TurnkeyContext.ErrorMessages));
						}

						TurnkeyUtils.Report("{0}@{1}: (Name={2}, Status={3}, Installed={4}, MinAllowed={5}, MaxAllowed={6}, Flags=\"{7}\"{8})", Platform, Device.Id, Device.Name, StatusString, Device.SoftwareVersion,
							MinSoftwareAllowedVersion, MaxSoftwareAllowedVersion, DeviceState.ToString(), DeviceErrorString);

						if (bForceDeviceInstall || !bIsSoftwareValid)
						{
							if (bUpdateIfNeeded)
							{
								if (Device.bCanConnect)
								{
									FileSource MatchingInstallableSdk = FileSource.FindMatchingSdk(AutomationPlatform, new FileSource.SourceType[] { FileSource.SourceType.Flash }, bSelectBest: bUnattended, DeviceType: Device.Type, CurrentSdk: Device.SoftwareVersion);

									if (MatchingInstallableSdk == null)
									{
										TurnkeyUtils.Log("ERROR: {0}: Unable to find any Sdks that could be installed on {1}", Platform, Device.Name);
										TurnkeyUtils.ExitCode = ExitCode.Error_SDKNotFound;
									}
									else
									{
										if (MatchingInstallableSdk.DownloadOrInstall(Platform, TurnkeyContext, Device, bUnattended) == false)
										{
											TurnkeyUtils.Log("Failed to update Device '{0}' with '{0}'", Device.Name, MatchingInstallableSdk.Name);
											TurnkeyUtils.ExitCode = ExitCode.Error_DeviceUpdateFailed;
										}
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
