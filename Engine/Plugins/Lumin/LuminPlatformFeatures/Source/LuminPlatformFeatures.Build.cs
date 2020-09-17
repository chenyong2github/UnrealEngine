// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class LuminPlatformFeatures : ModuleRules
	{
		public LuminPlatformFeatures(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Engine",
					"MLSDK"
				}
			);
		}
	}
}

