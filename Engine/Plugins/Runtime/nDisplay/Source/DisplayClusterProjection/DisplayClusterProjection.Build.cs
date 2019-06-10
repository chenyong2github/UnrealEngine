// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterProjection : ModuleRules
{
	public DisplayClusterProjection(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PrivateIncludePaths.AddRange(
			new string[]
			{
				"DisplayClusterProjection/Private",
                "../../../../Source/Runtime/Renderer/Private",
                "../../../../Source/Runtime/Engine/Classes/Components",
                "../../../../../Engine/Source/Runtime/Windows/D3D11RHI/Private",
                "../../../../../Engine/Source/Runtime/Windows/D3D11RHI/Private/Windows",

			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
                "D3D11RHI",
				"DisplayCluster",
				"Engine",
				"HeadMountedDisplay",
				"MPCDI",
				"RenderCore",
				"RHI",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

        AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");

        AddThirdPartyDependencies(ROTargetRules);
    }


    public bool AddThirdPartyDependencies(ReadOnlyTargetRules ROTargetRules)
    {
		string ModulePath = Path.GetDirectoryName(UnrealBuildTool.RulesCompiler.GetFileNameFromType(GetType()));
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModulePath, "../../ThirdParty/"));

		string PathInc = string.Empty;

		// EasyBlend
		PathInc = Path.Combine(ThirdPartyPath, "EasyBlend", "Include");
		PublicIncludePaths.Add(PathInc);
			   
        return true;
	}
}
