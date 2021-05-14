// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;

public class Protobuf : ModuleRules
{
	string GetVcPackageRoot(ReadOnlyTargetRules Target, string PackageName)
	{
		string TargetPlatform = null;
		string Platform = null;
		string Architecture = null;
		string Linkage = string.Empty;
		string Toolset = string.Empty;
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			TargetPlatform = "Win64";
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
			Toolset = "-v140";
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			Platform = "linux";
			Architecture = "x64";
		}

		if (string.IsNullOrEmpty(TargetPlatform) || string.IsNullOrEmpty(Platform) || string.IsNullOrEmpty(Architecture))
		{
			throw new System.NotSupportedException($"Platform {Target.Platform.ToString()} not currently supported by vcpkg");
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
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			LibraryExtension = ".a";
		}

		foreach (string Library in Libraries)
		{
			string LibraryPath = Path.Combine(VcPackageRoot, "lib", $"{Library}{LibraryExtension}");
			if (Target.Platform == UnrealTargetPlatform.Linux && !Library.StartsWith("lib"))
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

	public Protobuf(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform != UnrealTargetPlatform.Win64)
		{
			// Currently only supported for Win64 MSVC
			return;
		}

		PublicDependencyModuleNames.Add("zlib");

		// protobuf
		AddVcPackage(Target, "protobuf", true,
			"libprotobuf"
		);

		PublicDefinitions.Add("WITH_PROTOBUF");
	}
}
