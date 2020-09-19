// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;
using UnrealBuildTool;

public class Python3 : Python
{
	public Python3(ReadOnlyTargetRules Target) : base(Target)
	{
	}

	protected override List<PythonSDKPaths> GetPotentialPythonSDKs(ReadOnlyTargetRules Target)
	{
		var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
		
		var PythonBinaryTPSDir = Path.Combine(EngineDir, "Binaries", "ThirdParty", "Python3");
		var PythonSourceTPSDir = Path.Combine(EngineDir, "Source", "ThirdParty", "Python3");

		var PotentialSDKs = new List<PythonSDKPaths>();

		// todo: This isn't correct for cross-compilation, we need to consider the host platform too
		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			var PlatformDir = Target.Platform == UnrealTargetPlatform.Win32 ? "Win32" : "Win64";

			PotentialSDKs.AddRange(
				new PythonSDKPaths[] {
					new PythonSDKPaths(Path.Combine(PythonBinaryTPSDir, PlatformDir), new List<string>() { Path.Combine(PythonSourceTPSDir, PlatformDir, "include") }, new List<string>() { Path.Combine(PythonSourceTPSDir, PlatformDir, "libs", "python37.lib") }),
					//DiscoverPythonSDK("C:/Program Files/Python37"),
					DiscoverPythonSDK("C:/Python37"),
				}
			);
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PotentialSDKs.AddRange(
				new PythonSDKPaths[] {
					new PythonSDKPaths(Path.Combine(PythonBinaryTPSDir, "Mac"), new List<string>() { Path.Combine(PythonSourceTPSDir, "Mac", "include") }, new List<string>() { Path.Combine(PythonBinaryTPSDir, "Mac", "libpython3.7.dylib") }),
				}
			);
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.Architecture.StartsWith("x86_64"))
			{
				var PlatformDir = Target.Platform.ToString();
		
				PotentialSDKs.AddRange(
					new PythonSDKPaths[] {
						new PythonSDKPaths(
							Path.Combine(PythonBinaryTPSDir, PlatformDir),
							new List<string>() {
								Path.Combine(PythonSourceTPSDir, PlatformDir, "include", "python3.7"),
								Path.Combine(PythonSourceTPSDir, PlatformDir, "include", Target.Architecture)
							},
							new List<string>() { Path.Combine(PythonSourceTPSDir, PlatformDir, "lib", "libpython3.7.a") }),
				});
				PublicSystemLibraries.Add("util");	// part of libc
			}
		}
		
		return PotentialSDKs;
	}
	
	protected override void AppendPythonRuntimeDependencies(ReadOnlyTargetRules Target, bool IsEnginePython)
	{
		if (Target.Platform == UnrealTargetPlatform.Linux && IsEnginePython)
		{
			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Python/Linux/lib/libpython3.7.so.1.0");
		}
	}
}
