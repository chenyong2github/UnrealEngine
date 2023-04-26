// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class OpenColorIOWrapper : ModuleRules
	{
		public OpenColorIOWrapper(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PrivateIncludePathModuleNames.AddRange(new string[]
			{
				"Engine", // for TextureDefines.h
			});

			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ColorManagement",
			});

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("OpenColorIOLib");
			}
			else
			{
				PrivateDefinitions.Add("WITH_OCIO=0");
			}

			PublicDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"ImageCore"
			});
		}
	}
}
