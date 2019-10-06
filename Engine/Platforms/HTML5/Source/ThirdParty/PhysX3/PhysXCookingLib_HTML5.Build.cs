// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class PhysXCookingLib_HTML5 : PhysXCookingLib
{
	protected string PhysXVersion { get { return "PhysX_3.4"; } }
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }
	protected override string PhysXLibDir { get { return Path.Combine(LibRootDirectory, "PhysX3", PhysXVersion); } }

	public PhysXCookingLib_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		// library to link
		PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "PhysX3Cooking" + Target.HTML5OptimizationSuffix + ".bc"));
	}
}
