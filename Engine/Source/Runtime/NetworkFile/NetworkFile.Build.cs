// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NetworkFile : ModuleRules
{
	public NetworkFile(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("DerivedDataCache");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Sockets"
			});

		PublicIncludePaths.Add("Runtime/CoreUObject/Public/UObject");
		PublicIncludePaths.Add("Runtime/CoreUObject/Public");

		if (!Target.bBuildRequiresCookedData)
		{
			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					"DerivedDataCache",
				});
		}

		PublicDefinitions.Add("ENABLE_HTTP_FOR_NETWORK_FILE=0");
	}
}
