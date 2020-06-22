// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using UnrealBuildTool;
using AutomationTool;
using Tools.DotNETCommon;


namespace Turnkey.Commands
{
	class InstallSdk : TurnkeyCommand
	{
	
		protected override Dictionary<string, string[]> GetExtendedCommandsWithOptions()
		{
			return new Dictionary<string, string[]>()
			{
				{ "Auto Install All Needed Sdks", new string[] { "-platform=All", "-NeededOnly", "-BestAvailable" } },
				{ "Auto Update All Sdks", new string[] { "-platform=All", "-UpdateOnly", "-BestAvailable" } },
			};
		}


		protected override void Execute(string[] CommandOptions)
		{
			string DeviceName = TurnkeyUtils.ParseParamValue("Device", null, CommandOptions);
			string SdkTypeString = TurnkeyUtils.ParseParamValue("SdkType", null, CommandOptions);

			bool bBestAvailable = TurnkeyUtils.ParseParam("BestAvailable", CommandOptions);
			bool bUpdateOnly = TurnkeyUtils.ParseParam("UpdateOnly", CommandOptions);
			bool bAllowAutoSdk = TurnkeyUtils.ParseParam("AllowAutoSdk", CommandOptions);

			// best available installation requires valid and needed Sdks
			bool bValidOnly = bBestAvailable || !TurnkeyUtils.ParseParam("AllowInvalid", CommandOptions);
			bool bNeededOnly = bBestAvailable || TurnkeyUtils.ParseParam("NeededOnly", CommandOptions);


			SdkInfo.SdkType DesiredType = SdkInfo.SdkType.Misc;
			if (SdkTypeString != null)
			{
				if (!Enum.TryParse(SdkTypeString, out DesiredType))
				{
					TurnkeyUtils.Log("Invalid SdkType given with -SdkType={0}", SdkTypeString);
					return;
				}
			}

			// we need all sdks we can find
			List<SdkInfo> AllSdks = TurnkeyManifest.GetDiscoveredSdks();

			List<UnrealTargetPlatform> PlatformsWithSdks = new List<UnrealTargetPlatform>();
			// get all platforms we have Sdks for
			foreach (SdkInfo Sdk in AllSdks)
			{
				foreach (UnrealTargetPlatform SdkPlat in Sdk.GetPlatforms())
				{
					if (!PlatformsWithSdks.Contains(SdkPlat))
					{
						PlatformsWithSdks.Add(SdkPlat);
					}
				}
			}

			// get the platforms to install either from user or from commandline
			List<UnrealTargetPlatform> PlatformsLeftToInstall = TurnkeyUtils.GetPlatformsFromCommandLineOrUser(CommandOptions, PlatformsWithSdks);
			if (PlatformsLeftToInstall == null)
			{
				return;
			}

			List<SdkInfo> SdksAlreadyInstalled = new List<SdkInfo>();

			// keep going while we have Sdks left to install
			while (PlatformsLeftToInstall.Count > 0)
			{
				UnrealTargetPlatform Platform = PlatformsLeftToInstall[0];

				// remove the platform (we may remove more later if an Sdk supports multiple platforms)
				PlatformsLeftToInstall.RemoveAt(0);


				// cache the automation platform object
				AutomationTool.Platform AutomationPlatform = AutomationTool.Platform.GetPlatform(Platform);
				UEBuildPlatformSDK SDK = UEBuildPlatformSDK.GetSDKForPlatform(Platform.ToString());

				// 				if (bAllowAutoSdk)
				// 				{
				// 					// first, attempt AutoSdk
				// 					SdkInfo.LocalAvailability LocalState = SdkInfo.GetLocalAvailability(AutomationPlatform);
				// 					bool bWasAutoSdkSetup;
				// 					
				// 					SdkInfo.ConditionalSetupAutoSdk(Platform, ref LocalState, out bWasAutoSdkSetup, bUnattended: false);
				// 
				// 					// if we got an AutoSdk for this platform, then we don't need to continue
				// 					if (bWasAutoSdkSetup)
				// 					{
				// 						continue;
				// 					}
				// 				}


				// filter the Sdks if a platform was given
				List<SdkInfo> Sdks = AllSdks.FindAll(x => x.SupportsPlatform(Platform));

				// strip out flash Sdks where there are no devices
				int SdkCount = Sdks.Count;
				Sdks = Sdks.FindAll(x => x.Type != SdkInfo.SdkType.Flash || (AutomationPlatform.GetDevices() != null && AutomationPlatform.GetDevices().Length > 0));
				bool bStrippedDevices = Sdks.Count != SdkCount;

// 				// we don't need to do AutoSdks, we already attempted one above if we wanted to
// 				Sdks = Sdks.FindAll(x => x.Type != SdkInfo.SdkType.AutoSdk);


				// strip out Sdks where there is no Sdk installed yet
				if (bUpdateOnly)
				{
					Sdks = Sdks.FindAll(x => !string.IsNullOrEmpty(SDK.GetInstalledVersion()));
				}

				// strip out Sdks not in the allowed range of the platform
				if (bValidOnly)
				{
					Sdks = Sdks.FindAll(x => x.IsValid(Platform, DeviceName));
				}

				// strip out Sdks for platforms that are not needed to be updated
				if (bNeededOnly)
				{
					Sdks = Sdks.FindAll(x => x.IsNeeded(Platform, DeviceName));
				}

				// filter on type
				if (SdkTypeString != null && Sdks.Count > 0)
				{
					List<SdkInfo> TypedSdks = Sdks.FindAll(x => x.Type == DesiredType);
					if (TypedSdks.Count == 0)
					{
						TurnkeyUtils.Log("No valid Sdks found of type {0}, using Full", DesiredType);
						TypedSdks = Sdks.FindAll(x => x.Type == SdkInfo.SdkType.Full);
					}
					Sdks = TypedSdks;
				}

				if (bBestAvailable && Sdks.Count > 0)
				{
					// loop through Sdks that are left, and install the best one available
					Dictionary<SdkInfo.SdkType, SdkInfo> BestByType = new Dictionary<SdkInfo.SdkType, SdkInfo>();

					foreach (SdkInfo Sdk in Sdks)
					{
						// always install custom Sdks since there are no versions to check, if it's valid, let it decide what to do
						if (Sdk.CustomSdkId == null)
						{
							if (!BestByType.ContainsKey(Sdk.Type))
							{
								BestByType[Sdk.Type] = Sdk;
							}
							else
							{
								if (string.Compare(Sdk.Version, BestByType[Sdk.Type].Version, true) > 0)
								{
									BestByType[Sdk.Type] = Sdk;
								}
							}
						}
					}

					// first, keep only custom Sdks
					Sdks = Sdks.FindAll(x => x.CustomSdkId != null);

// 					// if the user chose a type, then pick that one
// 					if (SdkTypeString != null)
// 					{
// 						if ((DesiredType == SdkInfo.SdkType.BuildOnly || DesiredType == SdkInfo.SdkType.RunOnly) &&
// 							!BestByType.ContainsKey(DesiredType))
// 						{
// 							TurnkeyUtils.Log("No valid Sdks found of type {0}, trying for a Full",  DesiredType);
// 							DesiredType = SdkInfo.SdkType.Flash;
// 						}
// 
// 						if (!BestByType.ContainsKey(DesiredType))
// 						{
// 							TurnkeyUtils.Log("No valid Sdks found of type {0}, giving up.", DesiredType);
// 							return;
// 						}
// 
// 						SdkInfo Sdk = BestByType[DesiredType];
// 					}
// 					// otherwise, add all matches to the list
// 					else
					{
						Sdks.AddRange(BestByType.Values);
					}
				}
				// if we are not doing best available Sdks, then let used choose one
				else
				{
					if (Sdks.Count == 0)
					{
						TurnkeyUtils.Log("No Sdks found for platform {0}. Skipping", Platform);
						continue;
					}

					List<string> Options = new List<string>();
					foreach (SdkInfo Sdk in Sdks)
					{
						string Current = SDK.GetInstalledVersion();
						if (Sdk.Type == SdkInfo.SdkType.Flash)
						{
							// look for default device, or matching device [put this in a function!]
							DeviceInfo Device = GetDevice(AutomationPlatform, DeviceName);
							Current = Device == null ? "N/A" : Device.SoftwareVersion;
						}
						Options.Add(string.Format("[{0}] {1} [Current: {2}]", string.Join(",", Sdk.GetPlatforms()), Sdk.DisplayName, Current));
					}

					string Prompt = "Select an Sdk to install";
					if (bStrippedDevices)
					{
						Prompt += "\nNOTE: Some Flash Sdks were removed because no devices were found";
					}
					int Choice = TurnkeyUtils.ReadInputInt(Prompt, Options, true);

					if (Choice == 0)
					{
						continue;
					}

					// only install the chosen one
					SdkInfo ChosenSdk = Sdks[Choice - 1];
					Sdks = new List<SdkInfo>() { ChosenSdk };
				}


				foreach (SdkInfo Sdk in Sdks)
				{
					// because some Sdks can target muiltiple Sdks, if this one was already installed, don't need to reinstall it for other platforms
					if (SdksAlreadyInstalled.Contains(Sdk))
					{
						continue;
					}
					SdksAlreadyInstalled.Add(Sdk);

					DeviceInfo InstallDevice = null;
					// set variables
					if (Sdk.Type == SdkInfo.SdkType.Flash)
					{
						string InstallDeviceName = DeviceName;
						if (InstallDeviceName == null)
						{
							List<string> Options = new List<string>();
							DeviceInfo[] PossibleDevices = Array.FindAll(AutomationPlatform.GetDevices(), x => TurnkeyUtils.IsValueValid(x.Type, Sdk.AllowedFlashDeviceTypes, AutomationPlatform));
							foreach (DeviceInfo Device in PossibleDevices)
							{
								Options.Add(string.Format("[{0} {1}] {2}", Platform, Device.Type, Device.Name));
							}

							// get the choice
							int Choice = TurnkeyUtils.ReadInputInt("Select the number of a device to flash:", Options, true);

							if (Choice == 0)
							{
								return;
							}

							// get the name of the device chosen
							InstallDeviceName = PossibleDevices[Choice - 1].Name;
						}

						// get device info of the chosen or supplied device
						InstallDevice = Array.Find(AutomationPlatform.GetDevices(), x => string.Compare(x.Name, InstallDeviceName, true) == 0);

						if (InstallDevice == null)
						{
							TurnkeyUtils.Log("Unable to find {0} device {1}", Platform, InstallDeviceName);
							return;
						}
					}

					Sdk.Install(Platform, InstallDevice);
				}
			}
		}


		private DeviceInfo GetDevice(AutomationTool.Platform Platform, string DeviceName)
		{
			DeviceInfo[] Devices = Platform.GetDevices();
			if (Devices != null)
			{
				return Array.Find(Platform.GetDevices(), y => (DeviceName == null && y.bIsDefault) || (DeviceName != null && string.Compare(y.Name, DeviceName, true) == 0));
			}
			return null;
		}

	}
}
