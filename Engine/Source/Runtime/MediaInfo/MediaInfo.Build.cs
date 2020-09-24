// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MediaInfo : ModuleRules
	{
		public MediaInfo(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.Add("Core");
			PrivateDependencyModuleNames.Add("Media");
		}
	}
}
