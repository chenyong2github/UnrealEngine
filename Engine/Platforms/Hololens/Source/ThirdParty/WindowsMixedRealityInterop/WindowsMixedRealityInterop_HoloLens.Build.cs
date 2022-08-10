// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealityInterop_HoloLens : WindowsMixedRealityInterop
	{
		protected override bool bWithWMRI { get => true; }
		protected override string Architecture { get => Target.WindowsPlatform.GetArchitectureSubpath(); }

		public WindowsMixedRealityInterop_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			//HACK: use the release version of the interop because the debug build isn't compatible with UE right now.
			//         if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)  // Debug Win CRT compatibility problems prevent using the debug interop on x64 (which is for the device emulator). See also WindowsMixedRealityLibrary.Build.cs
			//{
			//	PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInteropHoloLensDebug.lib"));
			//}
			//else
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInteropHoloLens.lib"));
			}

			// Add a dependency to SceneUnderstanding.dll if present
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
