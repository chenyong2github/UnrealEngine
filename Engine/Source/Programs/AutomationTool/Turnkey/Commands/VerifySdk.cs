// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using Gauntlet;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnrealBuildTool;

namespace Turnkey.Commands
{
	class VerifySdk : TurnkeyCommand
	{
		protected override void Execute(string[] CommandOptions)
		{
			bool bUpdateIfNeeded = TurnkeyUtils.ParseParam("UpdateIfNeeded", CommandOptions);
			bool bUnattended = TurnkeyUtils.ParseParam("Unattended", CommandOptions);
			bool bPreferFullSdk = TurnkeyUtils.ParseParam("PreferFull", CommandOptions);
			bool bForceInstall = TurnkeyUtils.ParseParam("ForceInstall", CommandOptions);

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

				// get the platform object
				AutomationTool.Platform AutomationPlatform = AutomationTool.Platform.GetPlatform(Platform);

				AutomationPlatform.UpdateHostPrerequisites(TurnkeyUtils.CommandUtilHelper, Retriever);

				SdkInfo.LocalAvailability LocalState = SdkInfo.GetLocalAvailability(AutomationPlatform);

				if ((LocalState & (SdkInfo.LocalAvailability.AutoSdk_ValidVersionExists | SdkInfo.LocalAvailability.InstalledSdk_ValidVersionExists)) == 0)
				{
					TurnkeyUtils.Report("{0}: Invalid: [No AutoSdk or Installed Sdk found matching {1} - {2}]", Platform, AutomationPlatform.GetAllowedSdks(), LocalState.ToString());
					TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;
				}
				else
				{
					TurnkeyUtils.Report("{0}: Valid: [{1}]", Platform, (LocalState & (SdkInfo.LocalAvailability.AutoSdk_ValidVersionExists | SdkInfo.LocalAvailability.InstalledSdk_ValidVersionExists)).ToString());
					//					TurnkeyUtils.Log("{0}: Valid [Installed: '{1}', Required: '{2}']", Platform, PlatformObject.GetInstalledSdk(), PlatformObject.GetAllowedSdks());
				}

				// track if we installed an AutoSdk since we may need to setup autosdks before checking for devices
				bool bAutoSdkSetupSucceeded = false;

				// install if out of date, or if forcing it
				if (bForceInstall || (bUpdateIfNeeded && TurnkeyUtils.ExitCode != AutomationTool.ExitCode.Success))
				{
					TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Success;

					// if autosdk worked, we don't need to install full sdk
					if (!bAutoSdkSetupSucceeded)
					{
// 						if (!bUnattended)
// 						{
// 							string Response = TurnkeyUtils.ReadInput("Your Sdk installation is not up to date. Would you like to install a valid Sdk? [Y/n]", "Y");
// 							if (string.Compare(Response, "Y", true) != 0)
// 							{
// 								continue;
// 							}
// 						}

						SdkInfo BestSdk = null;
						// find the best Sdk, prioritizing as request
						if (bPreferFullSdk)
						{
							BestSdk = SdkInfo.FindMatchingSdk(AutomationPlatform, new SdkInfo.SdkType[] { SdkInfo.SdkType.Full, SdkInfo.SdkType.BuildOnly, SdkInfo.SdkType.AutoSdk }, bSelectBest: bUnattended);
						}
						else
						{
							BestSdk = SdkInfo.FindMatchingSdk(AutomationPlatform, new SdkInfo.SdkType[] { SdkInfo.SdkType.AutoSdk, SdkInfo.SdkType.BuildOnly, SdkInfo.SdkType.Full }, bSelectBest: bUnattended);
						}

						if (BestSdk == null)
						{
							TurnkeyUtils.Log("ERROR: {0}: Unable to find any Sdks that could be installed");
							TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;
							continue;
						}

						TurnkeyUtils.Log("Will install {0}", BestSdk.DisplayName);

						bool bWasSdkInstalled = BestSdk.Install(Platform, null, bUnattended);

						// update LocalState
						LocalState = SdkInfo.GetLocalAvailability(AutomationPlatform);

						// @todo turnkey: validate!
					}
				}

				// use the per-platform device list, unless it's not specifed, then use the global device list (which is set when not using platform specifiers)
				List<string> DeviceNames = Pair.Value != null ? Pair.Value : SplitDeviceList;
				if (DeviceNames != null && DeviceNames.Count > 0)
				{
					// if we just setup AutoSdk, then run the setup so that we have a hope of finding devices
					if (bAutoSdkSetupSucceeded)
					{
						// @todo turnkey: Run the autosdk thing
						//							AutomationPlatform.
					}

					DeviceInfo[] Devices = AutomationPlatform.GetDevices();
					if (Devices == null)
					{
						TurnkeyUtils.Log("Platform {0} didn't have any devices, ignoring any devices specified", Platform);
						continue;
					}

					TurnkeyUtils.Log("Installed Device validity:");

					// a single device named all means all devices
					if (!(DeviceNames.Count == 1 && DeviceNames[0].CompareTo("All") == 0))
					{
						Devices = Devices.Where(x => DeviceNames.Contains(x.Name, StringComparer.OrdinalIgnoreCase)).ToArray();
					}

					// now check software verison of each device
					foreach (DeviceInfo Device in Devices)
					{
						AutomationPlatform.UpdateDevicePrerequisites(Device, TurnkeyUtils.CommandUtilHelper, Retriever);

						bool bIsSoftwareValid = TurnkeyUtils.IsValueValid(Device.SoftwareVersion, AutomationPlatform.GetAllowedSoftwareVersions());


						if (bIsSoftwareValid)
						{
							TurnkeyUtils.Report("{0}@{1}: Valid: [{2}]", Platform, Device.Name, Device.SoftwareVersion);
						}
						else
						{
							TurnkeyUtils.Report("{0}@{1}: Invalid: [Has {2}, needs {3}]", Platform, Device.Name, Device.SoftwareVersion, AutomationPlatform.GetAllowedSoftwareVersions());
							TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;

							if (bUpdateIfNeeded)
							{
								SdkInfo MatchingInstallableSdk = SdkInfo.FindMatchingSdk(AutomationPlatform, new SdkInfo.SdkType[] { SdkInfo.SdkType.Flash }, bSelectBest: bUnattended);

								if (MatchingInstallableSdk == null)
								{
									TurnkeyUtils.Log("ERROR: {0}: Unable top find any Sdks that could be installed on {1}", Platform, Device.Name);
									TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;
								}
								else
								{
									MatchingInstallableSdk.Install(Platform, Device, bUnattended);
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
