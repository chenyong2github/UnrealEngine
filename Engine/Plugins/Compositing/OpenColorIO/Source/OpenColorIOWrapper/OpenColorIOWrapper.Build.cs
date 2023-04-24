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
				"OpenColorIOLib" // The OpenColorIO third-party library will only be available on desktop editor.
			});

			PublicDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"ImageCore"
			});
		}
	}
}
