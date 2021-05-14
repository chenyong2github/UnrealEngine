// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;

// TODO: Split apart dependency libs into separate External modules (double check dependencies)
// protobuf -> zlib
// grpc -> abseil, c-ares, protobuf, openssl, re2, ubp, zlib
// make base ModuleRules class for vcpkg
// Build with GOOGLE_PROTOBUF_NO_RTTI? https://github.com/protocolbuffers/protobuf/issues/5541
// Linux and MacOS support
public class Grpc : ModuleRules
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
			Toolset = "-v142";
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			Platform = "linux";
			Architecture = "x64";
		}

		if (string.IsNullOrEmpty(TargetPlatform) || string.IsNullOrEmpty(Platform) || string.IsNullOrEmpty(Architecture))
		{
			throw new System.NotSupportedException($"Platform {Target.Platform.ToString()} not currently supported by vcpkg");
			return string.Empty;
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
			return;
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
				return;
			}
			PublicAdditionalLibraries.Add(LibraryPath);
		}

		if (AddInclude)
		{
			string IncludePath = Path.Combine(VcPackageRoot, "include");
			if (!Directory.Exists(IncludePath))
			{
				throw new DirectoryNotFoundException(IncludePath);
				return;
			}

			PublicIncludePaths.Add(Path.Combine(VcPackageRoot, "include"));
		}
	}

	public Grpc(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform != UnrealTargetPlatform.Win64 || Target.WindowsPlatform.Compiler == WindowsCompiler.Clang)
		{
			// Currently only supported for Win64
			return;
		}

		PublicDependencyModuleNames.Add("OpenSSL");
		PublicDependencyModuleNames.Add("zlib");

		// protobuf
		AddVcPackage(Target, "protobuf", true,
			"libprotobuf"
		);

		// abseil
		AddVcPackage(Target, "abseil", false,
			"absl_bad_any_cast_impl",
			"absl_bad_optional_access",
			"absl_bad_variant_access",
			"absl_base",
			"absl_city",
			"absl_civil_time",
			"absl_cord",
			"absl_debugging_internal",
			"absl_demangle_internal",
			"absl_dynamic_annotations",
			"absl_examine_stack",
			"absl_exponential_biased",
			"absl_failure_signal_handler",
			"absl_flags",
			"absl_flags_config",
			"absl_flags_internal",
			"absl_flags_marshalling",
			"absl_flags_parse",
			"absl_flags_program_name",
			"absl_flags_registry",
			"absl_flags_usage",
			"absl_flags_usage_internal",
			"absl_graphcycles_internal",
			"absl_hash",
			"absl_hashtablez_sampler",
			"absl_int128",
			"absl_leak_check",
			"absl_leak_check_disable",
			"absl_log_severity",
			"absl_malloc_internal",
			"absl_periodic_sampler",
			"absl_random_distributions",
			"absl_random_internal_distribution_test_util",
			"absl_random_internal_pool_urbg",
			"absl_random_internal_randen",
			"absl_random_internal_randen_hwaes",
			"absl_random_internal_randen_hwaes_impl",
			"absl_random_internal_randen_slow",
			"absl_random_internal_seed_material",
			"absl_random_seed_gen_exception",
			"absl_random_seed_sequences",
			"absl_raw_hash_set",
			"absl_raw_logging_internal",
			"absl_scoped_set_env",
			"absl_spinlock_wait",
			"absl_stacktrace",
			"absl_status",
			"absl_strings",
			"absl_strings_internal",
			"absl_str_format_internal",
			"absl_symbolize",
			"absl_synchronization",
			"absl_throw_delegate",
			"absl_time",
			"absl_time_zone"
		);

		// grpc
		AddVcPackage(Target, "grpc", true,
			"address_sorting",
			"gpr",
			"grpc",
			"grpc_unsecure",
			"grpc++",
			"grpc++_unsecure"
		);

		// c-ares
		AddVcPackage(Target, "c-ares", false, "cares");

		// re2
		AddVcPackage(Target, "re2", false, "re2");

		// upb (micro-pb)
		AddVcPackage(Target, "upb", false,
			"handlers",
			"port",
			"reflection",
			"upb",
			"upb_json",
			"upb_pb"
		);
	}
}
