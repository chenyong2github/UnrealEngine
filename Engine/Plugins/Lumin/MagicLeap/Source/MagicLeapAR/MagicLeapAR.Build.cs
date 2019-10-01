// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapAR : ModuleRules
	{
		public MagicLeapAR(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateIncludePaths.AddRange(
				new string[] {
					"../../../../Source/Runtime/Renderer/Private",
					// ... add other private include paths required here ...
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"HeadMountedDisplay",
					"Core",
					"CoreUObject",
					"Engine",
					"RenderCore",
					"LuminRuntimeSettings",
					"MagicLeap",
					"MLSDK",
					"MagicLeapPlanes",
					"HeadMountedDisplay",
					"AugmentedReality",
				}
			);
		}
	}
}
