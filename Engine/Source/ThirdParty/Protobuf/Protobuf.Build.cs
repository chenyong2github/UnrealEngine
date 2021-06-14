// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Protobuf : ModuleRules
{
	public Protobuf(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (!IsVcPackageSupported)
		{
			return;
		}

		PublicDependencyModuleNames.Add("zlib");

		// protobuf
		AddVcPackage("protobuf", true,
			"libprotobuf"
		);

		PublicDefinitions.Add("WITH_PROTOBUF");
	}
}
