// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Asio : ModuleRules
{
	public Asio(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string AsioPath = Target.UEThirdPartySourceDirectory + "asio/1.12.2/";

		PublicSystemIncludePaths.Add(AsioPath);

		PublicDefinitions.Add("ASIO_SEPARATE_COMPILATION");
		PublicDefinitions.Add("ASIO_STANDALONE");
		PublicDefinitions.Add("ASIO_NO_EXCEPTIONS");
		PublicDefinitions.Add("ASIO_NO_TYPEID");
	}
}

