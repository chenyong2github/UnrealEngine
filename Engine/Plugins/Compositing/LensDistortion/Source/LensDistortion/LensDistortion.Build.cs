// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class LensDistortion : ModuleRules
	{
		public LensDistortion(ReadOnlyTargetRules Target) : base(Target)
		{
            
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"DeveloperSettings",
					"Engine",
					"RHI",
					"Engine",
					"RenderCore",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
                {
                    "Projects",
				}
			);
		}
	}
}
