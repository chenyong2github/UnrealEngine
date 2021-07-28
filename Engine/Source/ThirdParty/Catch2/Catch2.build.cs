// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Catch2 : ModuleRules
{
	public Catch2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string Catch2IncludePath = Target.UEThirdPartySourceDirectory + "Catch2/";
		PublicIncludePaths.Add(Catch2IncludePath);
	}
}
