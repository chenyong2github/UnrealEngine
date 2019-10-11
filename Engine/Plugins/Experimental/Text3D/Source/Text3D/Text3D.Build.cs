// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Text3D : ModuleRules
{
	public Text3D(ReadOnlyTargetRules Target) : base(Target)
	{
		bEnableExceptions = true;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDefinitions.Add("TEXT3D_WITH_FREETYPE=1");
		PrivateDefinitions.Add("TEXT3D_WITH_INTERSECTION=0");

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"SlateCore",
			"RenderCore",
			"RHI",

			// 3rd party libraries
			"FreeType2",
			"OpenGL",   // only for include purposes
			"FTGL",
			"HarfBuzz",
			"GLUtesselator"
		});
	}
}
