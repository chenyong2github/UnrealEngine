// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapController : ModuleRules
	{
		public MagicLeapController(ReadOnlyTargetRules Target)
				: base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"InputDevice"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"HeadMountedDisplay",
					"Core",
					"CoreUObject",
					"ApplicationCore",
					"Engine",
					"InputCore",
					"Slate",
					"SlateCore",
					"LuminRuntimeSettings",
					"MagicLeap",
					"MLSDK"
				}
			);
		}
	}
}
