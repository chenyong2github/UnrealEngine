// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ModelProtoConverter : ModuleRules
{
	public ModelProtoConverter( ReadOnlyTargetRules Target ) : base( Target )
	{
		// Define when ModelProtoConverter is available
		bool bIsModelProtoConverterSupported = (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.Mac);
		// if (bIsModelProtoConverterSupported)
		// {
		// 	PublicDefinitions.Add("WITH_MODEL_PROTO_CONVERTER_SUPPORT");
		// }

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "..")
			}
		);

		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Generated"));

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
				// "ONNXRuntimeProto",
				"ThirdPartyHelperAndDLLLoader"
			}
		);

		if (bIsModelProtoConverterSupported)
		{
			PrivateDependencyModuleNames.AddRange
				(
				new string[] {
					"Protobuf"
				}
			);
		}
	}
}