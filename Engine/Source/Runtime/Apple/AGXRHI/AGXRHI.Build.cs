// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class AGXRHI : ModuleRules
{	
	public AGXRHI(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Runtime/Apple/Common/Public");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"ApplicationCore",
				"Engine",
				"RHI",
				"RHICore",
				"RenderCore",
				"MetalCpp"
			}
			);

		AddEngineThirdPartyPrivateStaticDependencies(Target,
			"MTLPP"
		);
	}
}
