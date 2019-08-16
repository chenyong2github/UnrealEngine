// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using Microsoft.Win32;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class VisualStudioDTE : ModuleRules
	{
        public VisualStudioDTE(ReadOnlyTargetRules Target) : base(Target)
		{
			Type = ModuleType.External;

			PublicIncludePaths.Add(ModuleDirectory);

			bool bHasVisualStudioDTE;

			// In order to support building the plugin on build machines (which may not have the IDE installed), allow using an OLB rather than registered component.
			string DteOlbPath = Path.Combine(ModuleDirectory, "NotForLicensees", "dte80a.olb");
			if(File.Exists(DteOlbPath) && Target.WindowsPlatform.Compiler != WindowsCompiler.Clang)
			{
				PublicDefinitions.Add("WITH_VISUALSTUDIO_DTE_OLB=1");
				bHasVisualStudioDTE = true;
			}
			else
			{
				PublicDefinitions.Add("WITH_VISUALSTUDIO_DTE_OLB=0");
				try
				{
					// Interrogate the Win32 registry
					string DTEKey = null;
					switch (Target.WindowsPlatform.Compiler)
					{
                        case WindowsCompiler.VisualStudio2019:
                            DTEKey = "VisualStudio.DTE.16.0";
                            break;
                        case WindowsCompiler.VisualStudio2017:
                            DTEKey = "VisualStudio.DTE.15.0";
                            break;
                        case WindowsCompiler.VisualStudio2015_DEPRECATED:
							DTEKey = "VisualStudio.DTE.14.0";
							break;
					}
					bHasVisualStudioDTE = RegistryKey.OpenBaseKey(RegistryHive.ClassesRoot, RegistryView.Registry32).OpenSubKey(DTEKey) != null;
				}
				catch
				{
					bHasVisualStudioDTE = false;
				}
			}

			if (bHasVisualStudioDTE && Target.WindowsPlatform.StaticAnalyzer != WindowsStaticAnalyzer.PVSStudio)
			{
				PublicDefinitions.Add("WITH_VISUALSTUDIO_DTE=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_VISUALSTUDIO_DTE=0");
			}
		}
	}
}
