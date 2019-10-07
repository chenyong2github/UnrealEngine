// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class PhysXVehicleLib_HTML5 : PhysXVehicleLib
{
	// anyway to get his from PhysX?
	protected string PhysXVersion { get { return "PhysX_3.4"; } }
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }
	protected override string PhysXLibDir { get { return Path.Combine(LibRootDirectory, "PhysX3", PhysXVersion); } }

	public PhysXVehicleLib_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		// library to link
		PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "PhysX3Vehicle" + Target.HTML5OptimizationSuffix + ".bc"));
	}
}
