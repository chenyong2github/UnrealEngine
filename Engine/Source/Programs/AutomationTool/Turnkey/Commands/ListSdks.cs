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


			UnrealTargetPlatform? OptionalPlatform = null;
			FileSource.SourceType? OptionalType = null;

			if (TypeString != null)
			{
				FileSource.SourceType Type;
				if (Enum.TryParse(TypeString, out Type))
				{
					OptionalType = Type;
				}
			}

			if (PlatformString != null)
			{
				UnrealTargetPlatform Platform;
				if (UnrealTargetPlatform.TryParse(PlatformString, out Platform))
				{
					OptionalPlatform = Platform;
				}
			}

			List<FileSource> Sdks = TurnkeyManifest.FilterDiscoveredFileSources(OptionalPlatform, OptionalType);

			foreach (FileSource Sdk in Sdks)
			{
//				TurnkeyUtils.Log(Sdk.ToString(2));
				TurnkeyUtils.Log(Sdk.ToString());
			}
		}
	}
}
