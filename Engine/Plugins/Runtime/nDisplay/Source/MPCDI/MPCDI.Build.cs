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
				"Projects"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RenderCore",
				"RHI",
				"UtilityShaders"
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"../../../../../Engine/Source/Runtime/Windows/D3D11RHI/Private",
				"../../../../../Engine/Source/Runtime/Windows/D3D11RHI/Private/Windows",
				"../../../../../Engine/Source/Runtime/D3D12RHI/Private",
				"../../../../../Engine/Source/Runtime/D3D12RHI/Private/Windows"
			});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		PublicAdditionalLibraries.Add("opengl32.lib");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenGL");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "UElibPNG");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");

		AddThirdPartyDependencies(ROTargetRules);
	}

	public bool AddThirdPartyDependencies(ReadOnlyTargetRules ROTargetRules)
	{
		string ModulePath = Path.GetDirectoryName(UnrealBuildTool.RulesCompiler.GetFileNameFromType(GetType()));
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModulePath, "../../ThirdParty/"));

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

		return true;
	}
}
