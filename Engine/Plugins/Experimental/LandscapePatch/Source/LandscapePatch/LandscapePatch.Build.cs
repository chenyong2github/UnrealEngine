// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LandscapePatch : ModuleRules
{
	public LandscapePatch(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Landscape",
				"Engine",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RenderCore",
				"RHI",
				"Projects", // IPluginManager
				
				// This doesn't seem to be editor-only currently. If it becomes editor-only, may need
				// to inherit from LanscapeBlueprintBrushBase instead, and will lose the ability to
				// add the patch manager via the Blueprints tool in Landscape mode.
				"LandscapeEditorUtilities",
			}
			);
	}
}
