// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Xml.Serialization;
using System.IO;
using EpicGames.Core;
using UnrealBuildTool;
using AutomationTool;
using System.Linq;

namespace Turnkey
{
	static class SdkUtils
	{
		[Flags]
		public enum LocalAvailability
		{
			None = 0,
			AutoSdk_VariableExists = 1,
			AutoSdk_ValidVersionExists = 2,
			AutoSdk_InvalidVersionExists = 4,
			
			InstalledSdk_ValidInactiveVersionExists = 8,
			InstalledSdk_ValidVersionExists = 16,
			InstalledSdk_InvalidVersionExists = 32,

			Platform_ValidHostPrerequisites = 64,
			Platform_InvalidHostPrerequisites = 128,

			Device_InvalidPrerequisites = 256,
			Device_InstallSoftwareValid = 512,
			Device_InstallSoftwareInvalid = 1024,
			Device_CannotConnect = 2048,

			Support_FullSdk = 4096,
			Support_AutoSdk = 8192,
		}

		static public LocalAvailability GetLocalAvailability(AutomationTool.Platform AutomationPlatform, bool bAllowUpdatingPrerequisites, TurnkeyContextImpl TurnkeyContext)
		{
			LocalAvailability Result = LocalAvailability.None;

			// for some legacy NDA platforms, we could have an UnrealTargetPlatform but no registered SDK
			UEBuildPlatformSDK SDK = UEBuildPlatformSDK.GetSDKForPlatform(AutomationPlatform.PlatformType.ToString());
			if (SDK == null)
			{
				return Result;
			}

			if (AutomationPlatform.UpdateHostPrerequisites(TurnkeyUtils.CommandUtilHelper, TurnkeyContext, !bAllowUpdatingPrerequisites))
			{
				Result |= LocalAvailability.Platform_ValidHostPrerequisites;
			}
			else
			{
				Result |= LocalAvailability.Platform_InvalidHostPrerequisites;
			}

			string ManualSDKVersion, AutoSDKVersion;
			SDK.GetInstalledVersions(out ManualSDKVersion, out AutoSDKVersion);

			if (AutoSDKVersion == null)
			{
				// look to see if other versions are around
				string AutoSdkVar = Environment.GetEnvironmentVariable("UE_SDKS_ROOT");
				if (AutoSdkVar != null)
				{
					// no matter what, remember we have the variable set
					Result |= LocalAvailability.AutoSdk_VariableExists;

					// get platform subdirectory
					string AutoSubdir = string.Format("Host{0}/{1}", HostPlatform.Current.HostEditorPlatform.ToString(), SDK.GetAutoSDKPlatformName());
					DirectoryInfo PlatformDir = new DirectoryInfo(Path.Combine(AutoSdkVar, AutoSubdir));
					if (PlatformDir.Exists)
					{
						foreach (DirectoryInfo Dir in PlatformDir.EnumerateDirectories())
						{
							// look to see if other versions have been synced, but are bad version (otherwise, we would have had AutoSDKVersion above!)
							if (File.Exists(Path.Combine(Dir.FullName, "setup.bat")) || File.Exists(Path.Combine(Dir.FullName, "setup.sh")))
							{
								Result |= LocalAvailability.AutoSdk_InvalidVersionExists;
								break;
							}
						}
					}
				}

			}
			else
			{
				Result |= LocalAvailability.AutoSdk_ValidVersionExists | LocalAvailability.AutoSdk_VariableExists;
			}

			// if we have the variable at all, 

			// if anything is installed, this will return a value
			if (!string.IsNullOrEmpty(ManualSDKVersion))
			{
				if (SDK.IsVersionValid(ManualSDKVersion, bForAutoSDK: false))
				{
					Result |= LocalAvailability.InstalledSdk_ValidVersionExists;
				}
				else
				{
					Result |= LocalAvailability.InstalledSdk_InvalidVersionExists;
				}
			}


			// look for other, inactive, versions
			foreach (string AlternateVersion in SDK.GetAllInstalledSDKVersions())
			{
				if (SDK.IsVersionValid(AlternateVersion, bForAutoSDK: false))
				{
					Result |= LocalAvailability.InstalledSdk_ValidInactiveVersionExists;
				}
			}


			string FullSupportedPlatforms = TurnkeyUtils.GetVariableValue("Studio_FullInstallPlatforms");
			string AutoSdkSupportedPlatforms = TurnkeyUtils.GetVariableValue("Studio_AutoSdkPlatforms");
			if (!string.IsNullOrEmpty(FullSupportedPlatforms))
			{
				if (FullSupportedPlatforms.ToLower() == "all" || FullSupportedPlatforms.Split(",".ToCharArray()).Contains(AutomationPlatform.PlatformType.ToString(), StringComparer.InvariantCultureIgnoreCase))
				{
					Result |= LocalAvailability.Support_FullSdk;
				}
			}
			if (!string.IsNullOrEmpty(AutoSdkSupportedPlatforms))
			{
				if (AutoSdkSupportedPlatforms.ToLower() == "all" || AutoSdkSupportedPlatforms.Split(",".ToCharArray()).Contains(AutomationPlatform.PlatformType.ToString(), StringComparer.InvariantCultureIgnoreCase))
				{
					Result |= LocalAvailability.Support_AutoSdk;
				}
			}

			return Result;
		}


		public static bool SetupAutoSdk(FileSource Source, ITurnkeyContext TurnkeyContext, UnrealTargetPlatform Platform, bool bUnattended)
		{
			bool bSetupEnvVarAfterInstall = false;
			if (Environment.GetEnvironmentVariable("UE_SDKS_ROOT") == null)
			{
				if (bUnattended)
				{
					TurnkeyContext.ReportError($"Unable to install an AutoSDK without UE_SDKS_ROOT setup (can use Turnkey interactively to set it up)");
					return false;
				}

				bool bResponse = TurnkeyUtils.GetUserConfirmation("The AutoSdk system is not setup on this machine. Would you like to set it up now?", true);
				if (bResponse)
				{
					bSetupEnvVarAfterInstall = true;
				}
			}

			AutomationTool.Platform AutomationPlatform = AutomationTool.Platform.GetPlatform(Platform);

			TurnkeyUtils.Log("{0}: AutoSdk is setup on this computer, will look for available AutoSdk to download", Platform);

			// make sure this is unset so that we can know if it worked or not after install
			TurnkeyUtils.ClearVariable("CopyOutputPath");

			// now download it (AutoSdks don't "install") on download
			// @todo turnkey: handle errors, handle p4 going to wrong location, handle one Sdk for multiple platforms
			string CopyOperation = Source.GetCopySourceOperation();
			if (CopyOperation == null)
			{
				TurnkeyContext.ReportError($"Unable to find AutoSDK FileSource fopr {Platform}. Your Studio's TurnkeyManifest.xml file(s) may need to be fixed.");
				return false;
			}

			// download the AutoSDK
			string DownloadedRoot = CopyProvider.ExecuteCopy(CopyOperation);
			if (string.IsNullOrEmpty(DownloadedRoot))
			{
				TurnkeyContext.ReportError($"Unable to download the AutoSDK for {Platform}. Your Studio's TurnkeyManifest.xml file(s) may need to be fixed.");
				return false;
			}

			if (bSetupEnvVarAfterInstall)
			{
				// walk up to one above Host* directory
				DirectoryInfo AutoSdkSearch;
				if (Directory.Exists(DownloadedRoot))
				{
					AutoSdkSearch = new DirectoryInfo(DownloadedRoot);
				}
				else
				{
					AutoSdkSearch = new FileInfo(DownloadedRoot).Directory;
				}
				while (AutoSdkSearch.Name != "Host" + HostPlatform.Current.HostEditorPlatform.ToString())
				{
					AutoSdkSearch = AutoSdkSearch.Parent;
				}

				// now go one up to the parent of Host
				AutoSdkSearch = AutoSdkSearch.Parent;

				string AutoSdkDir = AutoSdkSearch.FullName;
				if (!bUnattended)
				{
					string Response = TurnkeyUtils.ReadInput("Enter directory for root of AutoSdks. Use detected value, or enter another:", AutoSdkSearch.FullName);
					if (string.IsNullOrEmpty(Response))
					{
						return false;
					}
				}

				// set the env var, globally
				TurnkeyUtils.StartTrackingExternalEnvVarChanges();
				Environment.SetEnvironmentVariable("UE_SDKS_ROOT", AutoSdkDir);
				Environment.SetEnvironmentVariable("UE_SDKS_ROOT", AutoSdkDir, EnvironmentVariableTarget.User);
				TurnkeyUtils.EndTrackingExternalEnvVarChanges();

			}

			// and now activate it in case we need it this run
			TurnkeyUtils.Log("Re-activating AutoSDK '{0}'...", Source.Name);

			UEBuildPlatformSDK.GetSDKForPlatform(Platform.ToString()).ReactivateAutoSDK();

			return true;
		}
	}
}
