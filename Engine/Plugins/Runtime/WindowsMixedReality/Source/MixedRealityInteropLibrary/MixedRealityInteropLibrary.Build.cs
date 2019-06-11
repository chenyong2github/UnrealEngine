// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;

public class MixedRealityInteropLibrary : ModuleRules
{
	public MixedRealityInteropLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibName = "MixedRealityInterop";
			if (Target.Configuration == UnrealTargetConfiguration.Debug)
			{
				LibName += "Debug";
			}
			string DLLName = LibName + ".dll";
			LibName += ".lib";

			PublicAdditionalLibraries.Add(LibName);
			// Delay-load the DLL, so we can load it from the right place first
			PublicDelayLoadDLLs.Add(DLLName);
			RuntimeDependencies.Add(PluginDirectory + "/Binaries/ThirdParty/MixedRealityInteropLibrary/" + Target.Platform.ToString() + "/" + DLLName);
			
			// Hologram remoting dlls
			if (Target.Platform == UnrealTargetPlatform.Win64 && Target.bBuildEditor == true)
			{
				string[] Dlls = { "HolographicAppRemoting.dll" , "PerceptionDevice.dll" };

				foreach(var Dll in Dlls)
				{
					PublicDelayLoadDLLs.Add(Dll);
					RuntimeDependencies.Add(string.Format("$(EngineDir)/Binaries/Win64/{0}", Dll));
				}
			}
		}
		else if(Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PublicAdditionalLibraries.Add("MixedRealityInteropHoloLens.lib");
		}
	}
}
