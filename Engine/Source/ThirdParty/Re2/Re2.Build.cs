// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;

// TODO: Split apart dependency libs into separate External modules (double check dependencies)
// grpc -> abseil, c-ares, protobuf, openssl, re2, ubp, zlib
// make base ModuleRules class for vcpkg
// MacOS support
public class Re2 : ModuleRules
{
	string GetVcPackageRoot(ReadOnlyTargetRules Target, string PackageName)
	{
		string TargetPlatform = Target.Platform.ToString();
		string Platform = null;
		string Architecture = null;
		string Linkage = string.Empty;
		string Toolset = string.Empty;
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			Platform = "windows";
			Architecture = Target.WindowsPlatform.Architecture.ToString().ToLowerInvariant();
			if (Target.bUseStaticCRT)
			{
				Linkage = "-static";
			}
			else
			{
				Linkage = "-static-md";
			}
			Toolset = "-v141";
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			Architecture = "x86_64";
			Platform = "unknown-linux-gnu";
		}
		else if (Target.Platform == UnrealTargetPlatform.LinuxAArch64)
		{
			Architecture = "aarch64";
			Platform = "unknown-linux-gnueabi";
		}

		if (string.IsNullOrEmpty(TargetPlatform) || string.IsNullOrEmpty(Platform) || string.IsNullOrEmpty(Architecture))
		{
			throw new System.NotSupportedException($"Platform {Target.Platform} not currently supported by vcpkg");
		}

		string Triplet = $"{Architecture}-{Platform}{Linkage}{Toolset}";

		return Path.Combine("ThirdParty", "vcpkg", TargetPlatform, Triplet, $"{PackageName}_{Triplet}");
	}

	void AddVcPackage(ReadOnlyTargetRules Target, string PackageName, bool AddInclude, params string[] Libraries)
	{
		string VcPackageRoot = GetVcPackageRoot(Target, PackageName);

		if (!Directory.Exists(VcPackageRoot))
		{
			throw new DirectoryNotFoundException(VcPackageRoot);
		}

		string LibraryExtension = string.Empty;
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			LibraryExtension = ".lib";
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.LinuxAArch64)
		{
			LibraryExtension = ".a";
		}

		foreach (string Library in Libraries)
		{
			string LibraryPath = Path.Combine(VcPackageRoot, "lib", $"{Library}{LibraryExtension}");
			if ((Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.LinuxAArch64) && !Library.StartsWith("lib"))
			{
				LibraryPath = Path.Combine(VcPackageRoot, "lib", $"lib{Library}{LibraryExtension}");
			}
			if (!File.Exists(LibraryPath))
			{
				throw new FileNotFoundException(LibraryPath);
			}
			PublicAdditionalLibraries.Add(LibraryPath);
		}

		if (AddInclude)
		{
			string IncludePath = Path.Combine(VcPackageRoot, "include");
			if (!Directory.Exists(IncludePath))
			{
				throw new DirectoryNotFoundException(IncludePath);
			}

			PublicIncludePaths.Add(Path.Combine(VcPackageRoot, "include"));
		}
	}

	public Re2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform != UnrealTargetPlatform.Win64)
		{
			// Currently only supported for Win64
			return;
		}

		AddVcPackage(Target, "re2", true, "re2");
	}
}
