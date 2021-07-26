// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// TODO: Split apart dependency libs into separate External modules (double check dependencies)
// grpc -> abseil, c-ares, protobuf, openssl, re2, ubp, zlib
// make base ModuleRules class for vcpkg
public class Grpc : ModuleRules
{
	public Grpc(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (!IsVcPackageSupported)
		{
			return;
		}
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// Required for c-ares
			PublicSystemLibraries.Add("resolv");
		}

		PublicDependencyModuleNames.Add("OpenSSL");
		PublicDependencyModuleNames.Add("zlib");
		PublicDependencyModuleNames.Add("Re2");
		PublicDependencyModuleNames.Add("Protobuf");

		// abseil
		AddVcPackage("abseil", true,
			"absl_bad_any_cast_impl",
			"absl_bad_optional_access",
			"absl_bad_variant_access",
			"absl_base",
			"absl_city",
			"absl_civil_time",
			"absl_cord",
			"absl_debugging_internal",
			"absl_demangle_internal",
			"absl_examine_stack",
			"absl_exponential_biased",
			"absl_failure_signal_handler",
			"absl_flags",
			"absl_flags_commandlineflag",
			"absl_flags_commandlineflag_internal",
			"absl_flags_config",
			"absl_flags_internal",
			"absl_flags_marshalling",
			"absl_flags_parse",
			"absl_flags_private_handle_accessor",
			"absl_flags_program_name",
			"absl_flags_reflection",
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
			"absl_random_internal_platform",
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
			"absl_statusor",
			"absl_strerror",
			"absl_strings",
			"absl_strings_internal",
			"absl_str_format_internal",
			"absl_symbolize",
			"absl_synchronization",
			"absl_throw_delegate",
			"absl_time",
			"absl_time_zone",
			"absl_wyhash"
		);

		// grpc
		AddVcPackage("grpc", true,
			"address_sorting",
			"gpr",
			"grpc++",
			"grpc++_alts",
			"grpc++_error_details",
			"grpc++_unsecure",
			"grpc",
			"grpc_plugin_support",
			"grpc_unsecure",
			"grpc_upbdefs"
		);

		// c-ares
		AddVcPackage("c-ares", false, "cares");

		// upb (micro-pb)
		AddVcPackage("upb", false,
			"upb",
			"upb_fastdecode",
			"upb_handlers",
			"upb_json",
			"upb_pb",
			"upb_reflection",
			"upb_textformat"
		);
	}
}
