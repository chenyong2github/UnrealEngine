// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using UnrealBuildTool;
using AutomationTool;
using Tools.DotNETCommon;
using System.Linq;

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


			FileSource.SourceType? DesiredType = null;
			if (SdkTypeString != null)
			{
				FileSource.SourceType OutType;
				if (!Enum.TryParse(SdkTypeString, out OutType))
				{
					TurnkeyUtils.Log("Invalid SdkType given with -SdkType={0}", SdkTypeString);
					return;
				}
				DesiredType = OutType;
			}

			// we need all sdks we can find
			List<UnrealTargetPlatform> PlatformsWithSdks = TurnkeyManifest.GetPlatformsWithSdks();

			// get the platforms to install either from user or from commandline
			List<UnrealTargetPlatform> PlatformsLeftToInstall = TurnkeyUtils.GetPlatformsFromCommandLineOrUser(CommandOptions, PlatformsWithSdks);
			if (PlatformsLeftToInstall == null)
			{
				return;
			}

			List<FileSource> SdksAlreadyInstalled = new List<FileSource>();

			// keep going while we have Sdks left to install
			while (PlatformsLeftToInstall.Count > 0)
			{
				UnrealTargetPlatform Platform = PlatformsLeftToInstall[0];

				// remove the platform (we may remove more later if an Sdk supports multiple platforms)
				PlatformsLeftToInstall.RemoveAt(0);


				// cache the automation platform object
				AutomationTool.Platform AutomationPlatform = AutomationTool.Platform.GetPlatform(Platform);
				UEBuildPlatformSDK SDK = UEBuildPlatformSDK.GetSDKForPlatform(Platform.ToString());

				// filter the Sdks if a platform was given, and type if it was given
				List<FileSource> Sdks = TurnkeyManifest.FilterDiscoveredFileSources(Platform, DesiredType);

				// strip out flash Sdks where there are no devices
				int SdkCount = Sdks.Count;
				Sdks = Sdks.FindAll(x => x.Type != FileSource.SourceType.Flash || (AutomationPlatform.GetDevices() != null && AutomationPlatform.GetDevices().Length > 0));
				bool bStrippedDevices = Sdks.Count != SdkCount;

				// skip Misc FileSources, as we dont know how they are sued
				Sdks = Sdks.FindAll(x => x.IsSdkType());

				if (bUpdateOnly)
				{
					// strip out Sdks where there is no Sdk installed yet
					Sdks = Sdks.FindAll(x => !string.IsNullOrEmpty(SDK.GetInstalledVersion()));
				}

// 				// strip out Sdks not in the allowed range of the platform
// 				if (bValidOnly)
// 				{
// 					Sdks = Sdks.FindAll(x => x.IsValid(Platform, DeviceName));
// 				}
// 
// 				// strip out Sdks for platforms that are not needed to be updated
// 				if (bNeededOnly)
// 				{
// 					Sdks = Sdks.FindAll(x => x.IsNeeded(Platform, DeviceName));
// 				}

				Sdks.Sort((a,b) => 
				{
					int Val = a.Type.CompareTo(b.Type);
					if (Val != 0)
					{
						return Val;
					}

					UInt64 VersionA, VersionB;
					if (SDK.TryConvertVersionToInt(a.Version, out VersionA) && SDK.TryConvertVersionToInt(b.Version, out VersionB))
					{
						Val = VersionA.CompareTo(VersionB);
						if (Val != 0)
						{
							return Val;
						}
						if (a.AllowedFlashDeviceTypes != null && b.AllowedFlashDeviceTypes != null)
						{
							return a.AllowedFlashDeviceTypes.CompareTo(b.AllowedFlashDeviceTypes);
						}
					}

					return 0;
				});

				if (bBestAvailable && Sdks.Count > 0)
				{
					// loop through Sdks that are left, and install the best one available
					Dictionary<FileSource.SourceType, FileSource> BestByType = new Dictionary<FileSource.SourceType, FileSource>();

					foreach (FileSource Sdk in Sdks)
					{
						if (!BestByType.ContainsKey(Sdk.Type))
						{
							BestByType[Sdk.Type] = Sdk;
						}
						else
						{
							// bigger version is better
							UInt64 VersionA, VersionB;
							if (SDK.TryConvertVersionToInt(Sdk.Version, out VersionA) && SDK.TryConvertVersionToInt(BestByType[Sdk.Type].Version, out VersionB) && VersionA > VersionB)
							{
								BestByType[Sdk.Type] = Sdk;
							}
						}
					}

					// get the best for all types
					Sdks.Clear();
					Sdks.AddRange(BestByType.Values);
 				}
				// if we are not doing best available Sdks, then let used choose one
				else
				{
					if (Sdks.Count == 0)
					{
						TurnkeyUtils.Log("No Sdks found for platform {0}. Skipping", Platform);
						if (bStrippedDevices)
						{
							TurnkeyUtils.Log("NOTE: Some Flash Sdks were removed because no devices were found");
						}
						continue;
					}

					List<string> Options = new List<string>();
					foreach (FileSource Sdk in Sdks)
					{
						string Current = SDK.GetInstalledVersion();
						if (Sdk.Type == FileSource.SourceType.Flash)
						{
							// look for default device, or matching device [put this in a function!]
							DeviceInfo Device = GetDevice(AutomationPlatform, DeviceName);
							Current = Device == null ? "N/A" : Device.SoftwareVersion;
						}
						Options.Add(string.Format("[{0}] {1} [Current: {2}]", string.Join(",", Sdk.GetPlatforms()), Sdk.Name, Current));
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
					FileSource ChosenSdk = Sdks[Choice - 1];
					Sdks = new List<FileSource>() { ChosenSdk };
				}

				foreach (FileSource Sdk in Sdks)
				{
					// because some Sdks can target muiltiple Sdks, if this one was already installed, don't need to reinstall it for other platforms
					if (SdksAlreadyInstalled.Contains(Sdk))
					{
						continue;
					}
					SdksAlreadyInstalled.Add(Sdk);

					DeviceInfo InstallDevice = null;
					// set variables
					if (Sdk.Type == FileSource.SourceType.Flash)
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

					Sdk.DownloadOrInstall(Platform, InstallDevice);
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
