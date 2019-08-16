// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class CrunchCompression : ModuleRules
	{
		public CrunchCompression(ReadOnlyTargetRules Target) : base(Target)
		{
           // PrivateIncludePaths.Add("Runtime/CrunchCompression/Private");
            PublicIncludePaths.Add("Runtime/CrunchCompression/Public");

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Crunch"
				}
			);
        }
	}
}
