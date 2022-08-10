// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealityHMD_HoloLens : WindowsMixedRealityHMD
	{
		protected override bool bSupportedPlatform { get => true; }

		public WindowsMixedRealityHMD_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "CoarseRelocUW.dll"));
			RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.Azure.SpatialAnchors.dll"));
			RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.Azure.SpatialAnchors.winmd"));
			PublicDelayLoadDLLs.Add("CoarseRelocUW.dll");
			PublicDelayLoadDLLs.Add("Microsoft.Azure.SpatialAnchors.dll");
			PublicDelayLoadDLLs.Add("Microsoft.MixedReality.QR.dll");
			RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.QR.dll"));

			string SceneUnderstandingPath = Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.SceneUnderstanding.dll");
			if (File.Exists(SceneUnderstandingPath))
			{
				RuntimeDependencies.Add(SceneUnderstandingPath);
				PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=0");
			}
		}
	}
}
