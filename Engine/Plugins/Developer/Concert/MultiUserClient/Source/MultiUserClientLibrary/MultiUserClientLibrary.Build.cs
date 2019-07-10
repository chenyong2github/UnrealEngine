// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MultiUserClientLibrary : ModuleRules
	{
		public MultiUserClientLibrary(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
				}
			);

			if (Target.bBuildDeveloperTools)
			{
				PrivateDefinitions.Add("WITH_CONCERT=1");

				PrivateIncludePathModuleNames.AddRange(
					new string[]
					{
						"Concert",
						"ConcertSyncCore",
						"ConcertSyncClient",
						"MultiUserClient",
					}
				);

				DynamicallyLoadedModuleNames.AddRange(
					new string[]
					{
						"MultiUserClient",
					}
				);
			}
			else
			{
				PrivateDefinitions.Add("WITH_CONCERT=0");
			}
		}
	}
}
