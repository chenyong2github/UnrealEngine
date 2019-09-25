// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AlembicHairTranslatorModule : ModuleRules
	{
        public AlembicHairTranslatorModule(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
					"AlembicLib",
                    "Core",
                    "Engine",
                    "HairStrandsCore",
                    "HairStrandsEditor"
                });
        }
	}
}
