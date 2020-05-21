// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using UnrealBuildTool;
using AutomationTool;
using System.Linq;

namespace Turnkey.Commands
{
	class ListSdks : TurnkeyCommand
	{
		protected override void Execute(string[] CommandOptions)
		{
			TurnkeyUtils.Log("");
			TurnkeyUtils.Log("Available Installers:");

			string TypeString = TurnkeyUtils.ParseParamValue("Type", null, CommandOptions);
			string PlatformString = TurnkeyUtils.ParseParamValue("Platform", null, CommandOptions);

			List<SdkInfo> Sdks = TurnkeyManifest.GetDiscoveredSdks();

			if (TypeString != null)
			{
				SdkInfo.SdkType Type;
				if (Enum.TryParse(TypeString, out Type))
				{
					Sdks = Sdks.FindAll(x => x.Type == Type);
				}
			}

			if (PlatformString != null)
			{
				UnrealTargetPlatform Platform;
				if (UnrealTargetPlatform.TryParse(PlatformString, out Platform))
				{
					Sdks = Sdks.FindAll(x => x.SupportsPlatform(Platform));
				}
			}


			foreach (SdkInfo Sdk in Sdks)
			{
				TurnkeyUtils.Log(Sdk.ToString(2));
			}
		}
	}
}
