// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class RemoteImportLibrary : ModuleRules
	{
		public RemoteImportLibrary(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(new string[]{
				"Core",
				"CoreUObject",
				"Engine",
				"RemoteImportMessaging",
			});
		}
	}
}
