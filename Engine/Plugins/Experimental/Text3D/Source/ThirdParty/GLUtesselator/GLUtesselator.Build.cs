// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GLUtesselator : ModuleRules
{
	public GLUtesselator(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		string Extension = "a";
		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
			Extension = "lib";

		if (Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Win64 ||
			Target.Platform == UnrealTargetPlatform.Mac ||
			Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", Target.Platform.ToString(), "GLU-tesselator." + Extension));
		}
	}
}
