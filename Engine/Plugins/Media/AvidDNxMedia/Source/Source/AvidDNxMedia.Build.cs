// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AvidDNxMedia : ModuleRules
	{
		public AvidDNxMedia(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"DNxHR",
					"DNxMXF",
					"DNxUncompressed",
					"Engine",
					"MovieSceneCapture",
					"Projects",
				}
			);
		}
	}
}
