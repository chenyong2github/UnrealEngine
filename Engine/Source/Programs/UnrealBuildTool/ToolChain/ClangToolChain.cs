// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using UnrealBuildBase;
using Microsoft.Extensions.Logging;
using System.IO;

namespace UnrealBuildTool
{
	/// <summary>
	/// Common option flags for the Clang toolchains.
	/// Usage of these flags is currently inconsistent between various toolchains.
	/// </summary>
	[Flags]
	enum ClangToolChainOptions
	{
		/// <summary>
		/// No custom options
		/// </summary>
		None = 0,

		/// <summary>
		/// Enable address sanitzier
		/// </summary>
		EnableAddressSanitizer = 1 << 0,

		/// <summary>
		/// Enable hardware address sanitzier
		/// </summary>
		EnableHWAddressSanitizer = 1 << 1,

		/// <summary>
		/// Enable thread sanitizer
		/// </summary>
		EnableThreadSanitizer = 1 << 2,

		/// <summary>
		/// Enable undefined behavior sanitizer
		/// </summary>
		EnableUndefinedBehaviorSanitizer = 1 << 3,

		/// <summary>
		/// Enable minimal undefined behavior sanitizer
		/// </summary>
		EnableMinimalUndefinedBehaviorSanitizer = 1 << 4,

		/// <summary>
		/// Enable memory sanitizer
		/// </summary>
		EnableMemorySanitizer = 1 << 5,

		/// <summary>
		/// Enable Shared library for the Sanitizers otherwise defaults to Statically linked
		/// </summary>
		EnableSharedSanitizer = 1 << 6,

		/// <summary>
		/// Enables link time optimization (LTO). Link times will significantly increase.
		/// </summary>
		EnableLinkTimeOptimization = 1 << 7,

		/// <summary>
		/// Enable thin LTO
		/// </summary>
		EnableThinLTO = 1 << 8,

		/// <summary>
		/// If should disable using objcopy to split the debug info into its own file or now
		/// When we support larger the 4GB files with objcopy.exe this can be removed!
		/// </summary>
		DisableSplitDebugInfoWithObjCopy = 1 << 9,

		/// <summary>
		/// Enable tuning of debug info for LLDB
		/// </summary>
		TuneDebugInfoForLLDB = 1 << 10,

		/// <summary>
		/// Whether or not to preserve the portable symbol file produced by dump_syms
		/// </summary>
		PreservePSYM = 1 << 11,

		/// <summary>
		/// (Apple toolchains) Whether we're outputting a dylib instead of an executable
		/// </summary>
		OutputDylib = 1 << 12,

		/// <summary>
		/// Enables the creation of custom symbol files used for runtime symbol resolution.
		/// </summary>
		GenerateSymbols = 1 << 13,

		/// <summary>
		/// Enables live code editing
		/// </summary>
		EnableLiveCodeEditing = 1 << 14,

		/// <summary>
		/// Enables dead code/data stripping and common code folding.
		/// </summary>
		EnableDeadStripping = 1 << 15,

		/// <summary>
		/// Indicates that the target is a moduler build i.e. Target.LinkType == TargetLinkType.Modular
		/// </summary>
		ModularBuild = 1 << 16
	}

	abstract class ClangToolChain : ISPCToolChain
	{
		// The Clang version being used to compile
		protected string? ClangVersionString = null;
		protected int ClangVersionMajor = -1;
		protected int ClangVersionMinor = -1;
		protected int ClangVersionPatch = -1;

		protected ClangToolChainOptions Options;

		public ClangToolChain(ClangToolChainOptions InOptions, ILogger InLogger)
			: base(InLogger)
		{
			Options = InOptions;
		}

		public override void FinalizeOutput(ReadOnlyTargetRules Target, TargetMakefileBuilder MakefileBuilder)
		{
			if (Target.bPrintToolChainTimingInfo && Target.bParseTimingInfoForTracing)
			{
				TargetMakefile Makefile = MakefileBuilder.Makefile;
				List<IExternalAction> CompileActions = Makefile.Actions.Where(x => x.ActionType == ActionType.Compile && x.ProducedItems.Any(i => i.HasExtension(".json"))).ToList();
				List<FileItem> TimingJsonFiles = CompileActions.SelectMany(a => a.ProducedItems.Where(i => i.HasExtension(".json"))).ToList();

				// Handing generating aggregate timing information if we compiled more than one file.
				if (TimingJsonFiles.Count > 0)
				{
					// Generate the file manifest for the aggregator.
					FileReference ManifestFile = FileReference.Combine(Makefile.ProjectIntermediateDirectory, $"{Target.Name}TimingManifest.csv");
					if (!DirectoryReference.Exists(ManifestFile.Directory))
					{
						DirectoryReference.CreateDirectory(ManifestFile.Directory);
					}
					File.WriteAllLines(ManifestFile.FullName, TimingJsonFiles.Select(f => f.FullName.Remove(f.FullName.Length - ".json".Length)));

					FileItem AggregateOutputFile = FileItem.GetItemByFileReference(FileReference.Combine(Makefile.ProjectIntermediateDirectory, $"{Target.Name}.trace.csv"));
					FileItem HeadersOutputFile = FileItem.GetItemByFileReference(FileReference.Combine(Makefile.ProjectIntermediateDirectory, $"{Target.Name}.headers.csv"));
					List<string> ActionArgs = new List<string>()
					{
						$"-AggregateFile={AggregateOutputFile.FullName}",
						$"-ManifestFile={ManifestFile.FullName}",
						$"-HeadersFile={HeadersOutputFile.FullName}",
					};

					Action AggregateTimingInfoAction = MakefileBuilder.CreateRecursiveAction<AggregateClangTimingInfo>(ActionType.ParseTimingInfo, string.Join(" ", ActionArgs));
					AggregateTimingInfoAction.WorkingDirectory = Unreal.EngineSourceDirectory;
					AggregateTimingInfoAction.StatusDescription = $"Aggregating {TimingJsonFiles.Count} Timing File(s)";
					AggregateTimingInfoAction.bCanExecuteRemotely = false;
					AggregateTimingInfoAction.bCanExecuteRemotelyWithSNDBS = false;
					AggregateTimingInfoAction.PrerequisiteItems.AddRange(TimingJsonFiles);

					AggregateTimingInfoAction.ProducedItems.Add(AggregateOutputFile);
					AggregateTimingInfoAction.ProducedItems.Add(HeadersOutputFile);
					Makefile.OutputItems.Add(AggregateOutputFile);
					Makefile.OutputItems.Add(HeadersOutputFile);
				}
			}
		}

		/// <summary>
		/// Sanitizes a preprocessor definition argument if needed.
		/// </summary>
		/// <param name="Definition">A string in the format "foo=bar" or "foo".</param>
		/// <returns>An escaped string</returns>
		protected virtual string EscapePreprocessorDefinition(string Definition)
		{
			// By default don't modify preprocessor definition, handle in platform overrides.
			return Definition;
		}

		/// <summary>
		/// Checks if compiler version matches the requirements
		/// </summary>
		protected bool CompilerVersionGreaterOrEqual(int Major, int Minor, int Patch)
		{
			// TODO: Temporary verification check to ensure a clang version has been set until ClangVersion is standarized across all clang-based toolchains.
			if (ClangVersionMajor == -1 || ClangVersionMinor == -1 || ClangVersionPatch == -1)
			{
				throw new BuildException($"ClangVersion not valid ({ClangVersionMajor}.{ClangVersionMinor}.{ClangVersionPatch}), unable to check compiler version requirements");
			}

			return new Version(ClangVersionMajor, ClangVersionMinor, ClangVersionPatch) >= new Version(Major, Minor, Patch);
		}

		/// <summary>
		/// Checks if compiler version matches the requirements
		/// </summary>
		protected bool CompilerVersionLessThan(int Major, int Minor, int Patch)
		{
			// TODO: Temporary verification check until ClangVersion is standarized across all clang-based toolchains to ensure a version has been set.
			if (ClangVersionMajor == -1 || ClangVersionMinor == -1 || ClangVersionPatch == -1)
			{
				throw new BuildException($"ClangVersion not valid ({ClangVersionMajor}.{ClangVersionMinor}.{ClangVersionPatch}), unable to check compiler version requirements");
			}

			return new Version(ClangVersionMajor, ClangVersionMinor, ClangVersionPatch) < new Version(Major, Minor, Patch);
		}

		protected virtual void GetCppStandardCompileArgument(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			switch (CompileEnvironment.CppStandard)
			{
				case CppStandardVersion.Cpp14:
					Arguments.Add("-std=c++14");
					break;
				case CppStandardVersion.Latest:
				case CppStandardVersion.Cpp17:
					Arguments.Add("-std=c++17");
					break;
				case CppStandardVersion.Cpp20:
					Arguments.Add("-std=c++20");
					break;
				default:
					throw new BuildException($"Unsupported C++ standard type set: {CompileEnvironment.CppStandard}");
			}

			if (CompileEnvironment.bEnableCoroutines)
			{
				Arguments.Add("-fcoroutines-ts");
				if (!CompileEnvironment.bEnableExceptions)
				{
					Arguments.Add("-Wno-coroutine-missing-unhandled-exception");
				}
			}
		}

		protected virtual void GetCompileArguments_CPP(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.Add("-x c++");
			GetCppStandardCompileArgument(CompileEnvironment, Arguments);
		}

		protected virtual void GetCompileArguments_C(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.Add("-x c");
		}

		protected virtual void GetCompileArguments_MM(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.Add("-x objective-c++");
			Arguments.Add("-fobjc-abi-version=2");
			Arguments.Add("-fobjc-legacy-dispatch");
			GetCppStandardCompileArgument(CompileEnvironment, Arguments);
		}

		protected virtual void GetCompileArguments_M(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.Add("-x objective-c");
			Arguments.Add("-fobjc-abi-version=2");
			Arguments.Add("-fobjc-legacy-dispatch");
			GetCppStandardCompileArgument(CompileEnvironment, Arguments);
		}

		protected virtual void GetCompileArguments_PCH(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.Add("-x c++-header");
			if (CompilerVersionGreaterOrEqual(11, 0, 0))
			{
				Arguments.Add("-fpch-validate-input-files-content");
				Arguments.Add("-fpch-instantiate-templates");
			}
			GetCppStandardCompileArgument(CompileEnvironment, Arguments);
		}

		// Conditionally enable (default disabled) generation of information about every class with virtual functions for use by the C++ runtime type identification features
		// (`dynamic_cast' and `typeid'). If you don't use those parts of the language, you can save some space by using -fno-rtti.
		// Note that exception handling uses the same information, but it will generate it as needed.
		protected virtual string GetRTTIFlag(CppCompileEnvironment CompileEnvironment)
		{
			return CompileEnvironment.bUseRTTI ? "-frtti" : "-fno-rtti";
		}

		protected virtual string GetUserIncludePathArgument(DirectoryReference IncludePath)
		{
			return $"-I\"{NormalizeCommandLinePath(IncludePath)}\"";
		}

		protected virtual string GetSystemIncludePathArgument(DirectoryReference IncludePath)
		{
			return $"-isystem\"{NormalizeCommandLinePath(IncludePath)}\"";
		}

		protected virtual void GetCompileArguments_IncludePaths(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.AddRange(CompileEnvironment.UserIncludePaths.Select(IncludePath => GetUserIncludePathArgument(IncludePath)));
			Arguments.AddRange(CompileEnvironment.SystemIncludePaths.Select(IncludePath => GetSystemIncludePathArgument(IncludePath)));
		}

		protected virtual string GetPreprocessorDefinitionArgument(string Definition)
		{
			return $"-D\"{EscapePreprocessorDefinition(Definition)}\"";
		}

		protected virtual void GetCompileArguments_PreprocessorDefinitions(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.AddRange(CompileEnvironment.Definitions.Select(Definition => GetPreprocessorDefinitionArgument(Definition)));
		}

		protected virtual string GetForceIncludeFileArgument(FileReference ForceIncludeFile)
		{
			return $"-include \"{NormalizeCommandLinePath(ForceIncludeFile)}\"";
		}

		protected virtual string GetForceIncludeFileArgument(FileItem ForceIncludeFile)
		{
			return GetForceIncludeFileArgument(ForceIncludeFile.Location);
		}

		protected virtual void GetCompileArguments_ForceInclude(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.AddRange(CompileEnvironment.ForceIncludeFiles.Select(ForceIncludeFile => GetForceIncludeFileArgument(ForceIncludeFile)));
		}

		/// <summary>
		/// Common compile arguments that control which warnings are enabled.
		/// https://clang.llvm.org/docs/DiagnosticsReference.html
		/// </summary>
		/// <param name="CompileEnvironment"></param>
		/// <param name="Arguments"></param>
		protected virtual void GetCompileArguments_WarningsAndErrors(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.Add("-Wall");                                     // https://clang.llvm.org/docs/DiagnosticsReference.html#wall
			Arguments.Add("-Werror");                                   // https://clang.llvm.org/docs/UsersManual.html#cmdoption-werror

			Arguments.Add("-Wdelete-non-virtual-dtor");                 // https://clang.llvm.org/docs/DiagnosticsReference.html#wdelete-non-virtual-dtor
			Arguments.Add("-Wenum-conversion");                         // https://clang.llvm.org/docs/DiagnosticsReference.html#wenum-conversion
			Arguments.Add("-Wbitfield-enum-conversion");                // https://clang.llvm.org/docs/DiagnosticsReference.html#wbitfield-enum-conversion

			Arguments.Add("-Wno-enum-enum-conversion");                 // https://clang.llvm.org/docs/DiagnosticsReference.html#wenum-enum-conversion					// ?? no reason given
			Arguments.Add("-Wno-enum-float-conversion");                // https://clang.llvm.org/docs/DiagnosticsReference.html#wenum-float-conversion					// ?? no reason given

			if (CompilerVersionGreaterOrEqual(13, 0, 0))
			{
				Arguments.Add("-Wno-unused-but-set-variable");           // https://clang.llvm.org/docs/DiagnosticsReference.html#wunused-but-set-variable				// new warning for clang 13
				Arguments.Add("-Wno-unused-but-set-parameter");          // https://clang.llvm.org/docs/DiagnosticsReference.html#wunused-but-set-parameter				// new warning for clang 13
				Arguments.Add("-Wno-ordered-compare-function-pointers"); // https://clang.llvm.org/docs/DiagnosticsReference.html#wordered-compare-function-pointers	// new warning for clang 13
			}

			Arguments.Add("-Wno-gnu-string-literal-operator-template"); // https://clang.llvm.org/docs/DiagnosticsReference.html#wgnu-string-literal-operator-template	// We use this feature to allow static FNames.
			Arguments.Add("-Wno-inconsistent-missing-override");        // https://clang.llvm.org/docs/DiagnosticsReference.html#winconsistent-missing-override			// ?? no reason given
			Arguments.Add("-Wno-invalid-offsetof");                     // https://clang.llvm.org/docs/DiagnosticsReference.html#winvalid-offsetof						// needed to suppress warnings about using offsetof on non-POD types.
			Arguments.Add("-Wno-switch");                               // https://clang.llvm.org/docs/DiagnosticsReference.html#wswitch								// this hides the "enumeration value 'XXXXX' not handled in switch [-Wswitch]" warnings - we should maybe remove this at some point and add UE_LOG(, Fatal, ) to default cases
			Arguments.Add("-Wno-tautological-compare");                 // https://clang.llvm.org/docs/DiagnosticsReference.html#wtautological-compare					// this hides the "warning : comparison of unsigned expression < 0 is always false" type warnings due to constant comparisons, which are possible with template arguments
			Arguments.Add("-Wno-unknown-pragmas");                      // https://clang.llvm.org/docs/DiagnosticsReference.html#wunknown-pragmas						// Slate triggers this (with its optimize on/off pragmas)
			Arguments.Add("-Wno-unused-function");                      // https://clang.llvm.org/docs/DiagnosticsReference.html#wunused-function						// this will hide the warnings about static functions in headers that aren't used in every single .cpp file
			Arguments.Add("-Wno-unused-lambda-capture");                // https://clang.llvm.org/docs/DiagnosticsReference.html#wunused-lambda-capture					// suppressed because capturing of compile-time constants is seemingly inconsistent. And MSVC doesn't do that.
			Arguments.Add("-Wno-unused-local-typedef");                 // https://clang.llvm.org/docs/DiagnosticsReference.html#wunused-local-typedef					// clang is being overly strict here? PhysX headers trigger this.
			Arguments.Add("-Wno-unused-private-field");                 // https://clang.llvm.org/docs/DiagnosticsReference.html#wunused-private-field					// this will prevent the issue of warnings for unused private variables. MultichannelTcpSocket.h triggers this, possibly more
			Arguments.Add("-Wno-unused-variable");                      // https://clang.llvm.org/docs/DiagnosticsReference.html#wunused-variable						// ?? no reason given
			Arguments.Add("-Wno-undefined-var-template");               // https://clang.llvm.org/docs/DiagnosticsReference.html#wundefined-var-template				// not really a good warning to disable

			// Profile Guided Optimization (PGO) and Link Time Optimization (LTO)
			if (CompileEnvironment.bPGOOptimize)
			{
				//
				// Clang emits warnings for each compiled object file that doesn't have a matching entry in the profile data.
				// This can happen when the profile data is older than the binaries we're compiling.
				//
				// Disable these warnings. They are far too verbose.
				//
				Arguments.Add("-Wno-profile-instr-out-of-date");        // https://clang.llvm.org/docs/DiagnosticsReference.html#wprofile-instr-out-of-date
				Arguments.Add("-Wno-profile-instr-unprofiled");         // https://clang.llvm.org/docs/DiagnosticsReference.html#wprofile-instr-unprofiled

				// apparently there can be hashing conflicts with PGO which can result in:
				// 'Function control flow change detected (hash mismatch)' warnings. 
				Arguments.Add("-Wno-backend-plugin");                   // https://clang.llvm.org/docs/DiagnosticsReference.html#wbackend-plugin
			}

			// shipping builds will cause this warning with "ensure", so disable only in those case
			if (CompileEnvironment.Configuration == CppConfiguration.Shipping)
			{
				Arguments.Add("-Wno-unused-value");                     // https://clang.llvm.org/docs/DiagnosticsReference.html#wunused-value
			}

			// https://clang.llvm.org/docs/DiagnosticsReference.html#wdeprecated-declarations
			if (CompileEnvironment.DeprecationWarningLevel == WarningLevel.Error)
			{
				// TODO: This may be unnecessary with -Werror
				Arguments.Add("-Werror=deprecated-declarations");
			}

			// https://clang.llvm.org/docs/DiagnosticsReference.html#wshadow
			if (CompileEnvironment.ShadowVariableWarningLevel != WarningLevel.Off)
			{
				Arguments.Add("-Wshadow" + ((CompileEnvironment.ShadowVariableWarningLevel == WarningLevel.Error) ? "" : " -Wno-error=shadow"));
			}

			// https://clang.llvm.org/docs/DiagnosticsReference.html#wundef
			if (CompileEnvironment.bEnableUndefinedIdentifierWarnings)
			{
				Arguments.Add("-Wundef" + (CompileEnvironment.bUndefinedIdentifierWarningsAsErrors ? "" : " -Wno-error=undef"));
			}

			// always use absolute paths for errors, this can help IDEs go to the error properly
			Arguments.Add("-fdiagnostics-absolute-paths");  // https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-fdiagnostics-absolute-paths

			// https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-cla-fcolor-diagnostics
			if (Log.ColorConsoleOutput)
			{
				Arguments.Add("-fdiagnostics-color");
			}

			// https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-fdiagnostics-format
			if (RuntimePlatform.IsWindows)
			{
				Arguments.Add("-fdiagnostics-format=msvc");
			}

			// Set the output directory for crashes
			// https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-fcrash-diagnostics-dir
			DirectoryReference? CrashDiagnosticDirectory = DirectoryReference.FromString(CompileEnvironment.CrashDiagnosticDirectory);
			if (CrashDiagnosticDirectory != null)
			{
				if (DirectoryReference.Exists(CrashDiagnosticDirectory))
				{
					Arguments.Add($"-fcrash-diagnostics-dir=\"{NormalizeCommandLinePath(CrashDiagnosticDirectory)}\"");
				}
				else
				{
					Log.TraceWarningOnce($"CrashDiagnosticDirectory has been specified but directory \"{CrashDiagnosticDirectory}\" does not exist. Compiler argument \"-fcrash-diagnostics-dir\" has been discarded.");
				}
			}
		}

		/// <summary>
		/// Compile arguments for optimization settings, such as profile guided optimization and link time optimization
		/// </summary>
		/// <param name="CompileEnvironment"></param>
		/// <param name="Arguments"></param>
		protected virtual void GetCompileArguments_Optimizations(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			// Profile Guided Optimization (PGO) and Link Time Optimization (LTO)
			if (CompileEnvironment.bPGOOptimize)
			{
				Log.TraceInformationOnce("Enabling Profile Guided Optimization (PGO). Linking will take a while.");
				Arguments.Add(string.Format("-fprofile-instr-use=\"{0}\"", Path.Combine(CompileEnvironment.PGODirectory!, CompileEnvironment.PGOFilenamePrefix!)));
			}
			else if (CompileEnvironment.bPGOProfile)
			{
				// Always enable LTO when generating PGO profile data.
				Log.TraceInformationOnce("Enabling Profile Guided Instrumentation (PGI). Linking will take a while.");
				Arguments.Add("-fprofile-instr-generate");
			}

			if (!CompileEnvironment.bUseInlining)
			{
				Arguments.Add("-fno-inline-functions");
			}
		}

		/// <summary>
		/// Compile arguments for debug settings
		/// </summary>
		/// <param name="CompileEnvironment"></param>
		/// <param name="Arguments"></param>
		protected virtual void GetCompileArguments_Debugging(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			// Optionally enable exception handling (off by default since it generates extra code needed to propagate exceptions)
			if (CompileEnvironment.bEnableExceptions)
			{
				Arguments.Add("-fexceptions");
				Arguments.Add("-DPLATFORM_EXCEPTIONS_DISABLED=0");
			}
			else
			{
				Arguments.Add("-fno-exceptions");
				Arguments.Add("-DPLATFORM_EXCEPTIONS_DISABLED=1");
			}
		}

		/// <summary>
		/// Compile arguments for sanitizers
		/// </summary>
		/// <param name="CompileEnvironment"></param>
		/// <param name="Arguments"></param>
		protected virtual void GetCompilerArguments_Sanitizers(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			// ASan
			if (Options.HasFlag(ClangToolChainOptions.EnableAddressSanitizer))
			{
				Arguments.Add("-fsanitize=address");
			}

			// TSan
			if (Options.HasFlag(ClangToolChainOptions.EnableThreadSanitizer))
			{
				Arguments.Add("-fsanitize=thread");
			}

			// UBSan
			if (Options.HasFlag(ClangToolChainOptions.EnableUndefinedBehaviorSanitizer))
			{
				Arguments.Add("-fsanitize=undefined");
			}

			// MSan
			if (Options.HasFlag(ClangToolChainOptions.EnableMemorySanitizer))
			{
				Arguments.Add("-fsanitize=memory");
			}
		}

		/// <summary>
		/// Common compile arguments for all files in a module.
		/// Override and call base.GetCompileArguments_Global() in derived classes.
		/// </summary>
		///
		/// <param name="CompileEnvironment"></param>
		/// <param name="Arguments"></param>
		protected virtual void GetCompileArguments_Global(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			// build up the commandline common to C and C++
			Arguments.Add("-c");
			Arguments.Add("-pipe");

			// Add include paths to the argument list.
			GetCompileArguments_IncludePaths(CompileEnvironment, Arguments);

			// Add preprocessor definitions to the argument list.
			GetCompileArguments_PreprocessorDefinitions(CompileEnvironment, Arguments);

			// Add warning and error flags to the argument list.
			GetCompileArguments_WarningsAndErrors(CompileEnvironment, Arguments);

			// Add optimization flags to the argument list.
			GetCompileArguments_Optimizations(CompileEnvironment, Arguments);

			// Add debugging flags to the argument list.
			GetCompileArguments_Debugging(CompileEnvironment, Arguments);

			// Add sanitizer flags to the argument list
			GetCompilerArguments_Sanitizers(CompileEnvironment, Arguments);
		}
	}
}