// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class APEX_HTML5 : APEX
{
	public APEX_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		// APEX CLOTH REQUIRES SIMD instructions
		throw new BuildException("Apex isn't fully supported (yet)");
	}
}
