// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DirectLink : ModuleRules
	{
		public DirectLink(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(new string[]{
				"Core",
				"Messaging",
			});

			PrivateDependencyModuleNames.AddRange(new string[]{
				"CoreUObject",
				"MessagingCommon",
			});
		}
	}
}
