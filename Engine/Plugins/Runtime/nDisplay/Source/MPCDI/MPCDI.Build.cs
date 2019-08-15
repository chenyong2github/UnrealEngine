// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class MPCDI : ModuleRules
{
	public MPCDI(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicDefinitions.Add("MPCDI_STATIC");

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Projects",
            });

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DisplayCluster",
				"RenderCore",
				"RHI"
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		AddEngineThirdPartyPrivateStaticDependencies(Target, "UElibPNG");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");

		AddThirdPartyDependencies(ROTargetRules);
	}

	public void AddThirdPartyDependencies(ReadOnlyTargetRules ROTargetRules)
	{
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/"));

		string PathLib = string.Empty;
		string PathInc = string.Empty;

		// MPCDI
		PathLib = Path.Combine(ThirdPartyPath, "MPCDI/Lib");
		PathInc = Path.Combine(ThirdPartyPath, "MPCDI/Include");
		
		// Libs
		PublicAdditionalLibraries.Add(Path.Combine(PathLib, "mpcdi.lib"));
		PublicAdditionalLibraries.Add(Path.Combine(PathLib, "tinyxml2.lib"));

		// Include paths		
		PublicIncludePaths.Add(PathInc);
		PublicIncludePaths.Add(Path.Combine(PathInc, "Base"));
		PublicIncludePaths.Add(Path.Combine(PathInc, "Container"));
		PublicIncludePaths.Add(Path.Combine(PathInc, "Creators"));
		PublicIncludePaths.Add(Path.Combine(PathInc, "IO"));
		PublicIncludePaths.Add(Path.Combine(PathInc, "Utils"));
	}
}
