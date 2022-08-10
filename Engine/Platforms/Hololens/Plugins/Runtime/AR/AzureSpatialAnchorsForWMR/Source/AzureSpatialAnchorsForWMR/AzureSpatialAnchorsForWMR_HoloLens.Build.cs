// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AzureSpatialAnchorsForWMR_HoloLens : AzureSpatialAnchorsForWMR
	{
		public AzureSpatialAnchorsForWMR_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "CoarseRelocUW.dll"));
			RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.Azure.SpatialAnchors.dll"));
			RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.Azure.SpatialAnchors.winmd"));

		}
	}
}
