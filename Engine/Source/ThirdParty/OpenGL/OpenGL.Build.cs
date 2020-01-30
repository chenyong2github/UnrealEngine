// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class OpenGL : ModuleRules
{
	public OpenGL(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicIncludePaths.Add(ModuleDirectory);

		if ((Target.Platform == UnrealTargetPlatform.Win64) ||
			(Target.Platform == UnrealTargetPlatform.Win32))
		{
			PublicSystemLibraries.Add("opengl32.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalFrameworks.Add(new Framework("OpenGL"));
			PublicAdditionalFrameworks.Add(new Framework("QuartzCore"));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicAdditionalFrameworks.Add(new Framework("OpenGLES"));
		}
	}
}
