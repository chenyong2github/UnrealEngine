// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Sockets_HTML5 : Sockets
{
	public Sockets_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDefinitions.Add("PLATFORM_SOCKETSUBSYSTEM=FName(TEXT(\"HTML5\"))");
	}
}
