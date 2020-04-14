// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DatasmithRemoteImport : ModuleRules
	{
		public DatasmithRemoteImport(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(new string[]{
				"Core",
			});

			PrivateDependencyModuleNames.AddRange(new string[]{
				"CoreUObject",
				"Engine",

				"RemoteImportLibrary",
				"MeshDescription",
				"DatasmithCore",
				"DatasmithTranslator",
			});
		}
	}
}
