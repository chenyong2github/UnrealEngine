// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GlsLang : ModuleRules
{
	public GlsLang(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(Target.UEThirdPartySourceDirectory + "glslang/glslang/src/glslang_lib");

		string LibPath = Target.UEThirdPartySourceDirectory + "glslang/glslang/lib/";
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			LibPath = LibPath + "Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
			
			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				PublicAdditionalLibraries.Add(LibPath + "/glslangd_64.lib");
			}
			else
			{
				PublicAdditionalLibraries.Add(LibPath + "/glslang_64.lib");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				PublicAdditionalLibraries.Add(LibPath + "Mac/libglslangd.a");
				PublicAdditionalLibraries.Add(LibPath + "Mac/libOSDependentd.a");
			}
			else
			{
				PublicAdditionalLibraries.Add(LibPath + "Mac/libglslang.a");
				PublicAdditionalLibraries.Add(LibPath + "Mac/libOSDependent.a");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicAdditionalLibraries.Add(LibPath + "Linux/" + Target.Architecture + "/libglslang.a");
			PublicAdditionalLibraries.Add(LibPath + "Linux/" + Target.Architecture + "/libHLSL.a");
			PublicAdditionalLibraries.Add(LibPath + "Linux/" + Target.Architecture + "/libOGLCompiler.a");
			PublicAdditionalLibraries.Add(LibPath + "Linux/" + Target.Architecture + "/libOSDependent.a");
			PublicAdditionalLibraries.Add(LibPath + "Linux/" + Target.Architecture + "/libSPIRV.a");
		}
		else
		{
			string Err = string.Format("Attempt to build against GlsLang on unsupported platform {0}", Target.Platform);
			System.Console.WriteLine(Err);
			throw new BuildException(Err);
		}
	}
}

