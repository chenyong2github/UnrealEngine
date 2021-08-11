// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ModelProtoFileReader : ModuleRules
{
	public ModelProtoFileReader( ReadOnlyTargetRules Target ) : base( Target )
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
				"CoreUObject",
				"Engine",
				"ModelProto"
			}
        );

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {
				"NeuralNetworkInferenceCore",
				"Projects",
				"Protobuf",
				"ThirdPartyHelperAndDLLLoader"
			}
		);
	}
}