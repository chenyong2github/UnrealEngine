// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NetworkFile_HTML5 : NetworkFile
{
	public NetworkFile_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDefinitions.Remove("ENABLE_HTTP_FOR_NETWORK_FILE=0");

		PublicDefinitions.Add("ENABLE_HTTP_FOR_NETWORK_FILE=1");
		PrivateDependencyModuleNames.Add("HTML5JS");
	}
}
