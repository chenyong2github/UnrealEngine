// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Asio : ModuleRules
{
	public Asio(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string AsioPath = Target.UEThirdPartySourceDirectory + "asio/1.12.2/";

		PublicSystemIncludePaths.Add(AsioPath);
	}
}

