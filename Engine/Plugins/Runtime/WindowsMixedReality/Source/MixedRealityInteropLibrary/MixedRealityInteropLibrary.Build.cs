// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;
using EpicGames.Core;

public class MixedRealityInteropLibrary : ModuleRules
{
	public MixedRealityInteropLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibName = "MixedRealityInterop";
			//HACK: use the release version of the interop because the debug build isn't compatible with UE right now.
			//if (Target.Configuration == UnrealTargetConfiguration.Debug)
			//{
			//	LibName += "Debug";
			//}
			string DLLName = LibName + ".dll";
			LibName += ".lib";

			string InteropLibPath = EngineDirectory + "/Source/ThirdParty/WindowsMixedRealityInterop/Lib/x64/";
			PublicAdditionalLibraries.Add(InteropLibPath + LibName);

			// Delay-load the DLL, so we can load it from the right place first
			PublicDelayLoadDLLs.Add(DLLName);
			RuntimeDependencies.Add(EngineDirectory + "/Binaries/ThirdParty/Windows/x64/" + DLLName);
			
			// Hologram remoting dlls
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				string[] Dlls = { "Microsoft.Holographic.AppRemoting.dll", "PerceptionDevice.dll", "Microsoft.MixedReality.QR.dll" };

				foreach(var Dll in Dlls)
				{
					PublicDelayLoadDLLs.Add(Dll);
					RuntimeDependencies.Add(EngineDirectory + "/Binaries/ThirdParty/Windows/x64/" + Dll);
				}
			}
		}
		else if(Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			string InteropLibPath = Target.UEThirdPartySourceDirectory + "/WindowsMixedRealityInterop/Lib/" + Target.WindowsPlatform.GetArchitectureSubpath() + "/";
			bool bUseDebugInteropLibrary = Target.Configuration == UnrealTargetConfiguration.Debug && Target.Architecture == "ARM64";
			if (Target.Configuration == UnrealTargetConfiguration.Debug && !bUseDebugInteropLibrary)
			{
				Log.TraceInformation("Building Hololens Debug {0} but not using MixedRealityInteropHoloLensDebug.lib due to Debug Win CRT incompatibilities.  Debugging within the interop will be similar to release.", Target.Architecture); // See also WindowsMixedRealityInterop.Build.cs
			}
			PublicAdditionalLibraries.Add(InteropLibPath + (bUseDebugInteropLibrary ? "MixedRealityInteropHoloLensDebug.lib" : "MixedRealityInteropHoloLens.lib"));
		}
	}
}
