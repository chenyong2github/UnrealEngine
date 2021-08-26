// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ModelProtoFileReader : ModuleRules
{
	public ModelProtoFileReader( ReadOnlyTargetRules Target ) : base( Target )
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "..")
			}
		);

		PublicDependencyModuleNames.AddRange
			(
			new string[] {
				"Core",
				"ModelProto"
			}
        );

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {
				"ModelProtoConverter"
			}
		);
	}
}