// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Diagnostics;
using System.Text;
using EpicGames.Core;
using System.Text.RegularExpressions;
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
		/// Checks if compiler version matches the requirements
		/// </summary>
		protected bool CompilerVersionGreaterOrEqual(int Major, int Minor, int Patch)
		{
			// TODO: Temporary verification check to ensure a clang version has been set until ClangVersion is standarized across all clang-based toolchains.
			if (ClangVersionMajor == -1)
			{
				throw new BuildException($"ClangVersion not found, unable to check compiler version requirements");
			}

			return ClangVersionMajor > Major ||
				(ClangVersionMajor == Major && ClangVersionMinor > Minor) ||
				(ClangVersionMajor == Major && ClangVersionMinor == Minor && ClangVersionPatch >= Patch);
		}

		/// <summary>
		/// Checks if compiler version matches the requirements
		/// </summary>
		protected bool CompilerVersionLessThan(int Major, int Minor, int Patch)
		{
			// TODO: Temporary verification check until ClangVersion is standarized across all clang-based toolchains to ensure a version has been set.
			if (ClangVersionMajor == -1)
			{
				throw new BuildException($"ClangVersion not found, unable to check compiler version requirements");
			}

			return ClangVersionMajor < Major ||
				(ClangVersionMajor == Major && ClangVersionMinor < Minor) ||
				(ClangVersionMajor == Major && ClangVersionMinor == Minor && ClangVersionPatch < Patch);
		}

		protected virtual string GetCppStandardCompileArgument(CppCompileEnvironment CompileEnvironment)
		{
			string Result;
			switch (CompileEnvironment.CppStandard)
			{
				case CppStandardVersion.Cpp14:
					Result = " -std=c++14";
					break;
				case CppStandardVersion.Latest:
				case CppStandardVersion.Cpp17:
					Result = " -std=c++17";
					break;
				case CppStandardVersion.Cpp20:
					Result = " -std=c++20";
					break;
				default:
					throw new BuildException($"Unsupported C++ standard type set: {CompileEnvironment.CppStandard}");
			}

			if (CompileEnvironment.bEnableCoroutines)
			{
				Result += " -fcoroutines-ts";
				if (!CompileEnvironment.bEnableExceptions)
				{
					Result += " -Wno-coroutine-missing-unhandled-exception";
				}
			}

			return Result;
		}

		protected virtual string GetCompileArguments_CPP(CppCompileEnvironment CompileEnvironment)
		{
			string Result = "";
			Result += " -x c++";
			Result += GetCppStandardCompileArgument(CompileEnvironment);
			return Result;
		}

		protected virtual string GetCompileArguments_C()
		{
			string Result = "";
			Result += " -x c";
			return Result;
		}

		protected virtual string GetCompileArguments_MM(CppCompileEnvironment CompileEnvironment)
		{
			string Result = "";
			Result += " -x objective-c++";
			Result += " -fobjc-abi-version=2";
			Result += " -fobjc-legacy-dispatch";
			Result += GetCppStandardCompileArgument(CompileEnvironment);
			return Result;
		}

		protected virtual string GetCompileArguments_M(CppCompileEnvironment CompileEnvironment)
		{
			string Result = "";
			Result += " -x objective-c";
			Result += " -fobjc-abi-version=2";
			Result += " -fobjc-legacy-dispatch";
			Result += GetCppStandardCompileArgument(CompileEnvironment);
			return Result;
		}

		protected virtual string GetCompileArguments_PCH(CppCompileEnvironment CompileEnvironment)
		{
			string Result = "";
			Result += " -x c++-header";
			if (CompilerVersionGreaterOrEqual(11, 0, 0))
			{
				Result += " -fpch-validate-input-files-content";
				Result += " -fpch-instantiate-templates";
			}
			Result += GetCppStandardCompileArgument(CompileEnvironment);
			return Result;
		}

		// Conditionally enable (default disabled) generation of information about every class with virtual functions for use by the C++ runtime type identification features
		// (`dynamic_cast' and `typeid'). If you don't use those parts of the language, you can save some space by using -fno-rtti.
		// Note that exception handling uses the same information, but it will generate it as needed.
		protected virtual string GetRTTIFlag(CppCompileEnvironment CompileEnvironment)
		{
			string Result;
			if (CompileEnvironment.bUseRTTI)
			{
				Result = " -frtti";
			}
			else
			{
				Result = " -fno-rtti";
			}
			return Result;
		}
	}
}