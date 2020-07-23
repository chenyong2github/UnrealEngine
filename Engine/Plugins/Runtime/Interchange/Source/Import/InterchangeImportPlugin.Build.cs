// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InterchangeImportPlugin : ModuleRules
	{
		public InterchangeImportPlugin(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InterchangeCore",
					"InterchangeNodePlugin",
				}
				);
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ImageWrapper",
					"RHI",
				}
				);
		}
	}
}
