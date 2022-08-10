// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class HoloLensAR_HoloLens : HoloLensAR
	{
		public HoloLensAR_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.QR.dll"));
			RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.QR.winmd"));

			// Add a dependency to SceneUnderstanding.dll if present
			string SceneUnderstandingDllPath = System.IO.Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.SceneUnderstanding.dll");
			string SceneUnderstandingWinMDPath = System.IO.Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.SceneUnderstanding.winmd");
			if (System.IO.File.Exists(SceneUnderstandingDllPath) && System.IO.File.Exists(SceneUnderstandingWinMDPath))
			{
				RuntimeDependencies.Add(SceneUnderstandingDllPath);
				RuntimeDependencies.Add(SceneUnderstandingWinMDPath);
				PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=0");
			}

			PrivateDependencyModuleNames.Add("D3D11RHI");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
		}
	}
}
