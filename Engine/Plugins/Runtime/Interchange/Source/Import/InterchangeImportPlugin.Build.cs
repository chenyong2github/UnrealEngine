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
					"InterchangeDispatcher",
					"InterchangeNodePlugin",
					"MeshDescription",
				}
				);
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ImageWrapper",
					"RHI",
					"InterchangeDispatcher",
					"Json",
				}
				);
		}
	}
}
