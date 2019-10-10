// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapScreens : ModuleRules
	{
		public MagicLeapScreens( ReadOnlyTargetRules Target ) : base(Target)
		{
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"LuminRuntimeSettings",
					"MLSDK",
					"MagicLeap",
					"HeadMountedDisplay",
				}
			);
		}
	}
}
