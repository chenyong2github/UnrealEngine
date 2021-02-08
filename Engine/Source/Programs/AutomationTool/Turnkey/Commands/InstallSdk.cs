// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using UnrealBuildTool;
using AutomationTool;
using EpicGames.Core;
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
				{ "Auto Update Installed Sdks", new string[] { "-platform=All", "-UpdateOnly", "-BestAvailable" } },
			};
		}


		protected override void Execute(string[] CommandOptions)
		{
			string DeviceName = TurnkeyUtils.ParseParamValue("Device", null, CommandOptions);
			string SdkTypeString = TurnkeyUtils.ParseParamValue("SdkType", null, CommandOptions);

			bool bUnattended = TurnkeyUtils.ParseParam("Unattended", CommandOptions);
			bool bBestAvailable = TurnkeyUtils.ParseParam("BestAvailable", CommandOptions);
			bool bUpdateOnly = TurnkeyUtils.ParseParam("UpdateOnly", CommandOptions);

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
			// if a device is specified, assume Flash type
			else if (!string.IsNullOrEmpty(DeviceName))
			{
				DesiredType = FileSource.SourceType.Flash;
			}
			// otherwise ask
			else if (!bUnattended)
			{
				List<string> Options = new List<string>() { "Full or Auto Sdk", "Full Sdk", "AutoSdk", "Device Software / Flash" };
				int Choice = TurnkeyUtils.ReadInputInt("Choose a type of Sdk to install:", Options, true, 1);

				if (Choice == 0)
				{
					return;
				}
				// Choice 1 means two types, so we use null DesiredType to mean Full|AutoSdk
				if (Choice == 2)
				{
					DesiredType = FileSource.SourceType.Full;
				}
				else if (Choice == 3)
				{
					DesiredType = FileSource.SourceType.AutoSdk;
				}
				else if (Choice == 4)
				{
					DesiredType = FileSource.SourceType.Flash;
				}
			}

			// we need all sdks we can find
			List<UnrealTargetPlatform> PlatformsWithSdks = TurnkeyManifest.GetPlatformsWithSdks();

			List<DeviceInfo> ChosenDevices = null;
			List<UnrealTargetPlatform> PlatformsLeftToInstall;

			if (DesiredType == FileSource.SourceType.Flash)
			{
				TurnkeyUtils.GetPlatformsAndDevicesFromCommandLineOrUser(CommandOptions, false, out PlatformsLeftToInstall, out ChosenDevices, PlatformsWithSdks);
				
				if (PlatformsLeftToInstall.Count == 0 || ChosenDevices.Count == 0)
				{
					return;
				}
			}
			else
			{
				PlatformsLeftToInstall = TurnkeyUtils.GetPlatformsFromCommandLineOrUser(CommandOptions, PlatformsWithSdks);

				if (PlatformsLeftToInstall == null || PlatformsLeftToInstall.Count == 0)
				{
					return;
				}
			}


			TurnkeyContextImpl TurnkeyContext = new TurnkeyContextImpl();

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
				List<FileSource> Sdks;
				if (DesiredType == null)
				{
					// get Full and Auto together
					Sdks = TurnkeyManifest.FilterDiscoveredFileSources(Platform, FileSource.SourceType.Full);
					Sdks.AddRange(TurnkeyManifest.FilterDiscoveredFileSources(Platform, FileSource.SourceType.AutoSdk));
				}
				else
				{
					Sdks = TurnkeyManifest.FilterDiscoveredFileSources(Platform, DesiredType);
				}

				DeviceInfo InstallDevice = null;
				if (DesiredType == FileSource.SourceType.Flash)
				{
					InstallDevice = ChosenDevices.First();
				}

				// skip Misc FileSources, as we dont know how they are sued
				Sdks = Sdks.FindAll(x => x.IsSdkType());

				if (bUpdateOnly)
				{
					// strip out Sdks where there is no Sdk installed yet
					Sdks = Sdks.FindAll(x => !string.IsNullOrEmpty(SDK.GetInstalledVersion()));
				}

				// strip out Sdks not in the allowed range of the platform
				bool bHadInvalidOptions = false;
				if (bValidOnly)
				{
					int BeforeCount = Sdks.Count;
					Sdks = Sdks.FindAll(x => x.IsVersionValid(Platform, InstallDevice));
					
					// track if we removed some invalid ones
					bHadInvalidOptions = Sdks.Count != BeforeCount;
				}

				//// strip out Sdks for platforms that are not needed to be updated
				//if (bNeededOnly)
				//{
				//	Sdks = Sdks.FindAll(x => x.IsNeeded(Platform, DeviceName));
				//}

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
					bool bAddShowMoreOption = bHadInvalidOptions && bValidOnly;

					if (Sdks.Count == 0 && !bAddShowMoreOption)
					{
						TurnkeyUtils.Log("No Sdks found for platform {0}. Skipping", Platform);
						//if (bStrippedDevices)
						//{
						//	TurnkeyUtils.Log("NOTE: Some Flash Sdks were removed because no devices were found");
						//}
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

					if (bAddShowMoreOption)
					{
						Options.Add("Show Invalid Sdks");
					}

					string Prompt = $"Select an Sdk to install:";
					//if (bStrippedDevices)
					//{
					//	Prompt += "\nNOTE: Some Flash Sdks were removed because no devices were found";
					//}
					int Choice = TurnkeyUtils.ReadInputInt(Prompt, Options, true);

					if (Choice == 0)
					{
						continue;
					}

					// if they chose the show more option, then add thius platform back in, and loop with bAllowInvaid to be true
					if (bAddShowMoreOption && Choice == Options.Count)
					{
						PlatformsLeftToInstall.Insert(0, Platform);
						bValidOnly = false;
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

					// make sure prerequisites are in good shape
					if (AutomationPlatform.UpdateHostPrerequisites(TurnkeyUtils.CommandUtilHelper, TurnkeyContext, false) == false)
					{
						TurnkeyContext.ReportError("Failed to update host prerequisites");
						continue;
					}
					if (InstallDevice != null && 
						AutomationPlatform.UpdateDevicePrerequisites(InstallDevice, TurnkeyUtils.CommandUtilHelper, TurnkeyContext, false) == false)
					{
						TurnkeyContext.ReportError("Failed to update device prerequisites");
						continue;
					}

					Sdk.DownloadOrInstall(Platform, TurnkeyContext, InstallDevice, false);
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
