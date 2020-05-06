using System;
using System.Collections.Generic;
using System.Linq;
using UnrealBuildTool;

namespace Turnkey.Commands
{
	class VerifySdk : TurnkeyCommand
	{
		protected override void Execute(string[] CommandOptions)
		{
			string DeviceName = TurnkeyUtils.ParseParamValue("Device", null, CommandOptions);

			// 		bool bBestAvailable = TurnkeyUtils.ParseParam("BestAvailable", CommandOptions);
			// 		bool bUpdateOnly = TurnkeyUtils.ParseParam("UpdateOnly", CommandOptions);

			List<UnrealTargetPlatform> ChosenPlatforms = TurnkeyUtils.GetPlatformsFromCommandLineOrUser(CommandOptions, UnrealTargetPlatform.GetValidPlatforms().ToList());

			if (ChosenPlatforms == null)
			{
				return;
			}

			TurnkeyUtils.Log("Installed Sdk validity:");
			foreach (UnrealTargetPlatform Platform in ChosenPlatforms)
			{
				// get the platform object
				AutomationTool.Platform PlatformObject = AutomationTool.Platform.Platforms[new AutomationTool.TargetPlatformDescriptor(Platform)];

				// check its installed Sdk is valid
				if (!TurnkeyUtils.IsValueValid(PlatformObject.GetInstalledSdk(), PlatformObject.GetAllowedSdks()))
				{
					TurnkeyUtils.Log("{0}: INVALID [Installed: '{1}', Required: '{2}']", Platform, PlatformObject.GetInstalledSdk(), PlatformObject.GetAllowedSdks());
					Environment.ExitCode = (int)AutomationTool.ExitCode.Error_SDKNotFound;
				}
				else
				{
					TurnkeyUtils.Log("{0}: Valid!", Platform);
//					TurnkeyUtils.Log("{0}: Valid [Installed: '{1}', Required: '{2}']", Platform, PlatformObject.GetInstalledSdk(), PlatformObject.GetAllowedSdks());
				}
			}
		}
	}
}
