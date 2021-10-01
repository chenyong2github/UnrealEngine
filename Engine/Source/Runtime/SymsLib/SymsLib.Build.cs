// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SymsLib : ModuleRules
{
	public SymsLib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		
		string LibPathBase =  ModuleDirectory + "/lib";

		PublicSystemIncludePaths.Add(ModuleDirectory);
		PublicSystemIncludePaths.Add(ModuleDirectory + "/syms");

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			bool bUseDebug = Target.Configuration == UnrealTargetConfiguration.Debug;
			string LibPath = LibPathBase + "/x64" + (bUseDebug ? "/Debug" : "/Release");
			PublicAdditionalLibraries.Add(LibPath + "/SymsLib.lib");
		}
	}
}
