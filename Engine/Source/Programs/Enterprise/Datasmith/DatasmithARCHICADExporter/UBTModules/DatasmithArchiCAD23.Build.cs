// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	[SupportedPlatforms("Win64", "Mac")]
	public class DatasmithArchiCADBase : ModuleRules
	{
		public DatasmithArchiCADBase(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PublicIncludePaths.Add("Runtime/Launch/Public");

			PrivateIncludePaths.Add("Runtime/Launch/Private");

			PrivateDependencyModuleNames.Add("Core");
			//PrivateDependencyModuleNames.Add("Projects");
		}
	}

	[SupportedPlatforms("Win64", "Mac")]
	public class DatasmithArchiCAD23 : DatasmithArchiCADBase
	{
		public DatasmithArchiCAD23(ReadOnlyTargetRules Target)
			: base(Target)
		{
		}
	}
}
