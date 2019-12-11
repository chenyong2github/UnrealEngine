//
// Copyright (C) Valve Corporation. All rights reserved.
//

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class SteamAudio : ModuleRules
	{
		public SteamAudio(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.AddRange(
				new string[] {
					"SteamAudio/Private",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"AudioMixer",
					"InputCore",
					"RenderCore",
					"RHI"
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"TargetPlatform",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"Projects",
					"AudioMixer"
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("UnrealEd");
				PrivateDependencyModuleNames.Add("Landscape");
			}

			AddEngineThirdPartyPrivateStaticDependencies(Target, "libPhonon");

			if (Target.Platform == UnrealTargetPlatform.Win32)
			{
				PrivateDependencyModuleNames.Add("XAudio2");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11Audio");
			}
			else if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PrivateDependencyModuleNames.Add("XAudio2");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11Audio");
				PublicDelayLoadDLLs.Add("GPUUtilities.dll");
				PublicDelayLoadDLLs.Add("tanrt64.dll");
				PublicDelayLoadDLLs.Add("embree.dll");
				PublicDelayLoadDLLs.Add("tbb.dll");
				PublicDelayLoadDLLs.Add("tbbmalloc.dll");
			}
			else if (Target.Platform == UnrealTargetPlatform.Android)
			{
				string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
				AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "SteamAudio_APL.xml"));
			}
		}
	}
}
