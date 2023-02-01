// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class BLAKE3_HoloLens : BLAKE3
	{
		public BLAKE3_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			string ModulePath = GetModuleDirectoryForSubClass(typeof(BLAKE3_HoloLens)).FullName;
			if (Target.WindowsPlatform.Architecture == UnrealArch.Arm64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(ModulePath, Version, "lib", "arm64", "Release", "BLAKE3.lib"));
			}
			else if (Target.WindowsPlatform.Architecture == UnrealArch.X64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(ModulePath, Version, "lib", "x64", "Release", "BLAKE3.lib"));
			}
		}
	}
}
