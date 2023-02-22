// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class CookedEditor : ModuleRules
{
	public CookedEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		if (!Target.bCompileAgainstEngine)
		{
			throw new BuildException("CookedEditor module is meant for cooking only operations, and currently requires Engine to be enabled. This module is being included in a non-Engine-enabled target.");
		}

		PrivateDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"AssetRegistry",
				"NetCore",
				"CoreOnline",
				"CoreUObject",
				"Projects",
				"Engine",
			});
		
		PublicDependencyModuleNames.AddRange(new string[]
			{
				"TargetPlatform",
			});

		PrivateIncludePathModuleNames.Add("WindowsTargetPlatform");

		// we currently need to pull in desktop templates that are private
		// @todo move these out to Public so they can be extended like this
		string EnginePath = Path.GetFullPath(Target.RelativeEnginePath);
		PublicIncludePaths.AddRange(
			new string[] {
				Path.Combine(EnginePath, "Source/Developer/Linux/LinuxTargetPlatform/Private"),
				Path.Combine(EnginePath, "Source/Developer/Mac/MacTargetPlatform/Private"),
			}
		);

	}
}
