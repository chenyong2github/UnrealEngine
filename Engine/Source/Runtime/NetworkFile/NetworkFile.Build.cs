// Copyright Epic Games, Inc. All Rights Reserved.

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
				"Networking",
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
