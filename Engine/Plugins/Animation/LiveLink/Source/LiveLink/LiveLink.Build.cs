// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class LiveLink : ModuleRules
	{
		public LiveLink(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"LiveLinkInterface",
				"LiveLinkMessageBusFramework",
			});

			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"HeadMountedDisplay",
				"InputCore",
				"Media",
				"Projects",
				"SlateCore",
				"Slate",
				"TimeManagement"
			});
		}
	}
}
