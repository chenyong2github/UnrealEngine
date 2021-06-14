// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Protobuf : ModuleRules
{
	public Protobuf(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform != UnrealTargetPlatform.Win64)
		{
			// Currently only supported for Win64
			return;
		}

		PublicDependencyModuleNames.Add("zlib");

		// protobuf
		AddVcPackage(Target, "protobuf", true,
			"libprotobuf"
		);

		PublicDefinitions.Add("WITH_PROTOBUF");
	}
}
