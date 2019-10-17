// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;


public class PhysX_HTML5 : PhysX
{
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }
	protected override string PhysXLibDir { get { return Path.Combine(LibRootDirectory, "PhysX3", PhysXVersion); } }

	public PhysX_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		string[] PhysXLibs = new string[]
		{
			"LowLevel",
			"LowLevelAABB",
			"LowLevelCloth",
			"LowLevelDynamics",
			"LowLevelParticles",
			"PhysX3",
			"PhysX3CharacterKinematic",
			"PhysX3Common",
			"PhysX3Cooking",
			"PhysX3Extensions",
			//"PhysXVisualDebuggerSDK",
			"SceneQuery",
			"SimulationController",
			"PxFoundation",
			"PxTask",
			"PxPvdSDK",
			"PsFastXml"
		};

		foreach (var lib in PhysXLibs)
		{
			PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, lib + Target.HTML5OptimizationSuffix + ".bc"));
		}
	}
}
