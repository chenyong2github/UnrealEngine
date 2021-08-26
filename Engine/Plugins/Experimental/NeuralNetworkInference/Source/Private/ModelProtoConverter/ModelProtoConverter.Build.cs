// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ModelProtoConverter : ModuleRules
{
	public ModelProtoConverter( ReadOnlyTargetRules Target ) : base( Target )
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Generated"));

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
				"Protobuf",
                "ThirdPartyHelperAndDLLLoader"
            }
		);
	}
}