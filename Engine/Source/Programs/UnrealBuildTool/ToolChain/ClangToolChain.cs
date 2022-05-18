// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	abstract class ClangToolChain : ISPCToolChain
	{
		// The Clang version being used to compile
		protected string? ClangVersionString = null;
		protected int ClangVersionMajor = -1;
		protected int ClangVersionMinor = -1;
		protected int ClangVersionPatch = -1;

		public ClangToolChain() : base()
		{
		}

		/// <summary>
		/// Normalize a path for use in a command line, making it relative to Engine/Source if under the root directory
		/// </summary>
		/// <param name="Reference">The FileSystemReference to normalize</param>
		/// <returns>Normalized path as a string</returns>
		protected static string NormalizeCommandLinePath(FileSystemReference Reference)
		{
			// Try to use a relative path to shorten command line length.
			if (Reference.IsUnderDirectory(Unreal.RootDirectory))
			{
				return Reference.MakeRelativeTo(UnrealBuildTool.EngineSourceDirectory).Replace("\\", "/");
			}

			return Reference.FullName.Replace("\\", "/");
		}

		/// <summary>
		/// Normalize a path for use in a command line, making it relative if under the Root Directory
		/// </summary>
		/// <param name="Item">The FileItem to normalize</param>
		/// <returns>Normalized path as a string</returns>
		protected static string NormalizeCommandLinePath(FileItem Item)
		{
			return NormalizeCommandLinePath(Item.Location);
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
			// TODO: System include paths can be included with -isystem
			return $"-I\"{NormalizeCommandLinePath(IncludePath)}\"";
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
			Arguments.Add("-Wall");     // https://clang.llvm.org/docs/DiagnosticsReference.html#wall
			Arguments.Add("-Werror");   // https://clang.llvm.org/docs/UsersManual.html#cmdoption-werror

			Arguments.Add("-Wdelete-non-virtual-dtor");     // https://clang.llvm.org/docs/DiagnosticsReference.html#wdelete-non-virtual-dtor
			Arguments.Add("-Wenum-conversion");             // https://clang.llvm.org/docs/DiagnosticsReference.html#wenum-conversion
			Arguments.Add("-Wbitfield-enum-conversion");    // https://clang.llvm.org/docs/DiagnosticsReference.html#wbitfield-enum-conversion

			Arguments.Add("-Wno-enum-float-conversion");    // https://clang.llvm.org/docs/DiagnosticsReference.html#wenum-float-conversion
			Arguments.Add("-Wno-enum-enum-conversion");     // https://clang.llvm.org/docs/DiagnosticsReference.html#wenum-enum-conversion

			// Profile Guided Optimization (PGO) and Link Time Optimization (LTO)
			if (CompileEnvironment.bPGOOptimize)
			{
				//
				// Clang emits warnings for each compiled object file that doesn't have a matching entry in the profile data.
				// This can happen when the profile data is older than the binaries we're compiling.
				//
				// Disable these warnings. They are far too verbose.
				//
				Arguments.Add("-Wno-profile-instr-out-of-date");    // https://clang.llvm.org/docs/DiagnosticsReference.html#wprofile-instr-out-of-date
				Arguments.Add("-Wno-profile-instr-unprofiled");     // https://clang.llvm.org/docs/DiagnosticsReference.html#wprofile-instr-unprofiled

				// apparently there can be hashing conflicts with PGO which can result in:
				// 'Function control flow change detected (hash mismatch)' warnings. 
				Arguments.Add("-Wno-backend-plugin");               // https://clang.llvm.org/docs/DiagnosticsReference.html#wbackend-plugin
			}

			// shipping builds will cause this warning with "ensure", so disable only in those case
			if (CompileEnvironment.Configuration == CppConfiguration.Shipping)
			{
				Arguments.Add("-Wno-unused-value"); // https://clang.llvm.org/docs/DiagnosticsReference.html#wunused-value
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
		}
	}
}