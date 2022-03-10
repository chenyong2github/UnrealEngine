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
	abstract class AppleToolChainSettings
	{
		/// <summary>
		/// Which developer directory to root from? If this is "xcode-select", UBT will query for the currently selected Xcode
		/// </summary>
		public string XcodeDeveloperDir = "xcode-select";

		public AppleToolChainSettings(bool bVerbose)
		{
			SelectXcode(ref XcodeDeveloperDir, bVerbose);
		}

		private static void SelectXcode(ref string DeveloperDir, bool bVerbose)
		{
			string Reason = "hardcoded";

			if (DeveloperDir == "xcode-select")
			{
				Reason = "xcode-select";

				// on the Mac, run xcode-select directly
				DeveloperDir = Utils.RunLocalProcessAndReturnStdOut("xcode-select", "--print-path");

				// make sure we get a full path
				if (Directory.Exists(DeveloperDir) == false)
				{
					throw new BuildException("Selected Xcode ('{0}') doesn't exist, cannot continue.", DeveloperDir);
				}

				if (DeveloperDir.EndsWith("/") == false)
				{
					// we expect this to end with a slash
					DeveloperDir += "/";
				}
			}

			if (bVerbose && !DeveloperDir.StartsWith("/Applications/Xcode.app"))
			{
				Log.TraceInformationOnce("Compiling with non-standard Xcode ({0}): {1}", Reason, DeveloperDir);
			}

			// Installed engine requires Xcode 11
			if (Unreal.IsEngineInstalled())
			{
				string? InstalledSdkVersion = UnrealBuildBase.ApplePlatformSDK.InstalledSDKVersion;
				if (String.IsNullOrEmpty(InstalledSdkVersion))
				{
					throw new BuildException("Unable to get xcode version");
				}
				if (int.Parse(InstalledSdkVersion.Substring(0,2)) < 11)
				{
					throw new BuildException("Building for macOS, iOS and tvOS requires Xcode 11 or newer, Xcode " + InstalledSdkVersion + " detected");
				}
			}
		}

		protected void SelectSDK(string BaseSDKDir, string OSPrefix, ref string PlatformSDKVersion, bool bVerbose)
		{
			if (PlatformSDKVersion == "latest")
			{
				PlatformSDKVersion = "";
				try
				{
					// on the Mac, we can just get the directory name
					string[] SubDirs = System.IO.Directory.GetDirectories(BaseSDKDir);

					// loop over the subdirs and parse out the version
					int MaxSDKVersionMajor = 0;
					int MaxSDKVersionMinor = 0;
					string? MaxSDKVersionString = null;
					foreach (string SubDir in SubDirs)
					{
						string SubDirName = Path.GetFileNameWithoutExtension(SubDir);
						if (SubDirName.StartsWith(OSPrefix))
						{
							// get the SDK version from the directory name
							string SDKString = SubDirName.Replace(OSPrefix, "");
							int Major = 0;
							int Minor = 0;

							// parse it into whole and fractional parts (since 10.10 > 10.9 in versions, but not in math)
							try
							{
								string[] Tokens = SDKString.Split(".".ToCharArray());
								if (Tokens.Length == 2)
								{
									Major = int.Parse(Tokens[0]);
									Minor = int.Parse(Tokens[1]);
								}
							}
							catch (Exception)
							{
								// weirdly formatted SDKs
								continue;
							}

							// update largest SDK version number
							if (Major > MaxSDKVersionMajor || (Major == MaxSDKVersionMajor && Minor > MaxSDKVersionMinor))
							{
								MaxSDKVersionString = SDKString;
								MaxSDKVersionMajor = Major;
								MaxSDKVersionMinor = Minor;
							}
						}
					}

					// use the largest version
					if (MaxSDKVersionString != null)
					{
						PlatformSDKVersion = MaxSDKVersionString;
					}
				}
				catch (Exception Ex)
				{
					// on any exception, just use the backup version
					Log.TraceInformation("Triggered an exception while looking for SDK directory in Xcode.app");
					Log.TraceInformation("{0}", Ex.ToString());
				}
			}

			// make sure we have a valid SDK directory
			if (!RuntimePlatform.IsWindows && !Directory.Exists(Path.Combine(BaseSDKDir, OSPrefix + PlatformSDKVersion + ".sdk")))
			{
				throw new BuildException("Invalid SDK {0}{1}.sdk, not found in {2}", OSPrefix, PlatformSDKVersion, BaseSDKDir);
			}

			if (bVerbose && !ProjectFileGenerator.bGenerateProjectFiles)
			{
				Log.TraceInformation("Compiling with {0} SDK {1}", OSPrefix, PlatformSDKVersion);
			}
		}
	}

	abstract class AppleToolChain : ISPCToolChain
	{
		protected FileReference? ProjectFile;

		public AppleToolChain(FileReference? InProjectFile)
		{
			ProjectFile = InProjectFile;
		}

		protected DirectoryReference GetMacDevSrcRoot()
		{
			return UnrealBuildTool.EngineSourceDirectory;
		}

		protected void StripSymbolsWithXcode(FileReference SourceFile, FileReference TargetFile, string ToolchainDir)
		{
			if (SourceFile != TargetFile)
			{
				// Strip command only works in place so we need to copy original if target is different
				File.Copy(SourceFile.FullName, TargetFile.FullName, true);
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.FileName = Path.Combine(ToolchainDir, "strip");
			StartInfo.Arguments = String.Format("\"{0}\" -S", TargetFile.FullName);
			StartInfo.UseShellExecute = false;
			StartInfo.CreateNoWindow = true;
			Utils.RunLocalProcessAndLogOutput(StartInfo);
		}

		static Version? ClangVersion = null;
		static string? FullClangVersion = null;

		protected Version GetClangVersion()
		{	
			if (ClangVersion == null)
			{
				FileReference ClangLocation = new FileReference("/usr/bin/clang");
				ClangVersion = RunToolAndCaptureVersion(ClangLocation, "--version");			
			}
			return ClangVersion;
		}

		protected string GetFullClangVersion()
		{
			if (FullClangVersion == null)
			{
				// get the first line that has the full clang and build number
				FileReference ClangLocation = new FileReference("/usr/bin/clang");
				FullClangVersion = RunToolAndCaptureOutput(ClangLocation, "--version", "(.*)")!;
			}

			return FullClangVersion;
		}

		protected static string GetCppStandardCompileArgument(CppCompileEnvironment CompileEnvironment)
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

		protected virtual string GetObjCExceptionsFlag(CppCompileEnvironment CompileEnvironment)
		{
			return "";
		}

		protected string GetPCHCompileArgumentsForCompileEnvironment(CppCompileEnvironment CompileEnvironment)
		{
			StringBuilder PCHArguments = new StringBuilder();
			if (CompileEnvironment.PrecompiledHeaderAction == PrecompiledHeaderAction.Include)
			{
				// Add the precompiled header file's path to the include path so GCC can find it.
				// This needs to be before the other include paths to ensure GCC uses it instead of the source header file.
				PCHArguments.Append(" -include \"");
				PCHArguments.Append(CompileEnvironment.PrecompiledHeaderIncludeFilename!.FullName);
				PCHArguments.Append("\"");
			}

			return PCHArguments.ToString();
		}

		protected virtual string GetCompileArgumentsForCompileEnvironment(CppCompileEnvironment CompileEnvironment)
		{
			StringBuilder Arguments = new StringBuilder();
			Arguments.Append(GetCompileArguments_Global(CompileEnvironment));

			// Add include paths to the argument list.
			HashSet<DirectoryReference> AllIncludes = new HashSet<DirectoryReference>(CompileEnvironment.UserIncludePaths);
			AllIncludes.UnionWith(CompileEnvironment.SystemIncludePaths);
			foreach (DirectoryReference IncludePath in AllIncludes)
			{
				Arguments.Append(" -I\"");
				Arguments.Append(IncludePath.FullName);
				Arguments.Append("\"");
			}

			foreach (string Definition in CompileEnvironment.Definitions)
			{
				string DefinitionArgument = Definition.Contains("\"") ? Definition.Replace("\"", "\\\"") : Definition;
				Arguments.Append(" -D\"");
				Arguments.Append(DefinitionArgument);
				Arguments.Append("\"");
			}

			return Arguments.ToString();
		}

		public virtual string GetCompileArgumentsForAbsoluteFilePathWithCompileEnvironment(string FilePath, CppCompileEnvironment CompileEnvironment)
		{
			string Arguments = GetCompileArgumentsForCompileEnvironment(CompileEnvironment);
			string PCHArguments = GetPCHCompileArgumentsForCompileEnvironment(CompileEnvironment);

			foreach (FileItem ForceIncludeFile in CompileEnvironment.ForceIncludeFiles)
			{
				Arguments += String.Format(" -include \"{0}\"", ForceIncludeFile.Location.FullName);
			}

			string FileArguments = "";
			string Extension = Path.GetExtension(FilePath).ToUpperInvariant();

			if (CompileEnvironment.PrecompiledHeaderAction == PrecompiledHeaderAction.Create)
			{
				// Compile the file as a C++ PCH.
				FileArguments += GetCompileArguments_PCH(CompileEnvironment);
				FileArguments += GetRTTIFlag(CompileEnvironment);
				FileArguments += GetObjCExceptionsFlag(CompileEnvironment);
			}
			else if (Extension == ".C")
			{
				// Compile the file as C code.
				FileArguments += GetCompileArguments_C();
			}
			else if (Extension == ".MM")
			{
				// Compile the file as Objective-C++ code.
				FileArguments += GetCompileArguments_MM(CompileEnvironment);
				FileArguments += GetRTTIFlag(CompileEnvironment);
				FileArguments += GetObjCExceptionsFlag(CompileEnvironment);
			}
			else if (Extension == ".M")
			{
				// Compile the file as Objective-C++ code.
				FileArguments += GetCompileArguments_M(CompileEnvironment);
				FileArguments += GetObjCExceptionsFlag(CompileEnvironment);
			}
			else
			{
				// Compile the file as C++ code.
				FileArguments += GetCompileArguments_CPP(CompileEnvironment);
				FileArguments += GetRTTIFlag(CompileEnvironment);
				FileArguments += GetObjCExceptionsFlag(CompileEnvironment);

				// only use PCH for .cpp files
				Arguments = PCHArguments + Arguments;
			}

			string AllArgs = Arguments + FileArguments;

			return AllArgs;
		}

		protected string GetDsymutilPath(out string ExtraOptions, bool bIsForLTOBuild=false)
		{
			FileReference DsymutilLocation = new FileReference("/usr/bin/dsymutil");

			// dsymutil before 10.0.1 has a bug that causes issues, it's fixed in autosdks but not everyone has those set up so for the timebeing we have
			// a version in P4 - first determine if we need it
			string DsymutilVersionString = Utils.RunLocalProcessAndReturnStdOut(DsymutilLocation.FullName, "-version");

			bool bUseInstalledDsymutil = true;
			int Major = 0, Minor = 0, Patch = 0;

			// tease out the version number
			string[] Tokens = DsymutilVersionString.Split(" ".ToCharArray());
			
			// sanity check
			if (Tokens.Length < 4 || Tokens[3].Contains(".") == false)
			{
				Log.TraceInformationOnce("Unable to parse dsymutil version out of: {0}", DsymutilVersionString);
			}
			else
			{
				string[] Versions = Tokens[3].Split(".".ToCharArray());
				if (Versions.Length < 3)
				{
					Log.TraceInformationOnce("Unable to parse version token: {0}", Tokens[3]);
				}
				else
				{
					if (!int.TryParse(Versions[0], out Major) || !int.TryParse(Versions[1], out Minor) || !int.TryParse(Versions[2], out Patch))
					{
						Log.TraceInformationOnce("Unable to parse version tokens: {0}", Tokens[3]);
					}
					else
					{
						if (Major < 12)
						{
							Log.TraceInformationOnce("dsymutil version is {0}.{1}.{2}. Using bundled version.", Major, Minor, Patch);
							bUseInstalledDsymutil = false;
						}
					}
				}
			}

			// if the installed one is too old, use a fixed up one if it can
			if (bUseInstalledDsymutil == false)
			{
				FileReference PatchedDsymutilLocation = FileReference.Combine(Unreal.EngineDirectory, "Restricted/NotForLicensees/Binaries/Mac/LLVM/bin/dsymutil");

				if (File.Exists(PatchedDsymutilLocation.FullName))
				{
					DsymutilLocation = PatchedDsymutilLocation;
				}

				DirectoryReference? AutoSdkDir;
				if (UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out AutoSdkDir))
				{
					FileReference AutoSdkDsymutilLocation = FileReference.Combine(AutoSdkDir, "Mac", "LLVM", "bin", "dsymutil");
					if (FileReference.Exists(AutoSdkDsymutilLocation))
					{
						DsymutilLocation = AutoSdkDsymutilLocation;
					}
				}
			}

			// 10.0.1 has an issue with LTO builds where we need to limit the number of threads
			ExtraOptions = (bIsForLTOBuild && Major == 10 && Minor == 0 && Patch == 1) ? "-j 1" : "";
			return DsymutilLocation.FullName;
		}

		protected virtual string GetCompileArguments_Global(CppCompileEnvironment CompileEnvironment)
		{
			string Result = "";

			Result += " -fmessage-length=0";
			Result += " -pipe";
			Result += " -fpascal-strings";
			Result += " -Wall -Werror";
			Result += " -Wdelete-non-virtual-dtor";

			// clang 12.00 has a new warning for copies in ranged loops. Instances have all been fixed up (2020/6/26) but
			// are likely to be reintroduced due to no equivalent on other platforms at this time so disable the warning
			// See also MacToolChain.cs
			if (GetClangVersion().Major >= 12)
			{
				Result += " -Wno-range-loop-analysis";
			}

			if (CompileEnvironment.ShadowVariableWarningLevel != WarningLevel.Off)
			{
				Result += " -Wshadow" + ((CompileEnvironment.ShadowVariableWarningLevel == WarningLevel.Error) ? "" : " -Wno-error=shadow");
			}

			if (CompileEnvironment.bEnableUndefinedIdentifierWarnings)
			{
				Result += " -Wundef" + (CompileEnvironment.bUndefinedIdentifierWarningsAsErrors ? "" : " -Wno-error=undef");
			}

			Result += " -c";

			if (!CompileEnvironment.bUseInlining)
			{
				Result += " -fno-inline-functions";
			}

			// Create DWARF format debug info if wanted,
			if (CompileEnvironment.bCreateDebugInfo)
			{
				Result += " -gdwarf-2";
			}

			return Result;
		}

		static string GetCompileArguments_CPP(CppCompileEnvironment CompileEnvironment)
		{
			string Result = "";
			Result += " -x objective-c++";
			Result += GetCppStandardCompileArgument(CompileEnvironment);
			Result += " -stdlib=libc++";

			return Result;
		}

		static string GetCompileArguments_MM(CppCompileEnvironment CompileEnvironment)
		{
			string Result = "";
			Result += " -x objective-c++";
			Result += GetCppStandardCompileArgument(CompileEnvironment);
			Result += " -stdlib=libc++";
			return Result;
		}

		static string GetCompileArguments_M(CppCompileEnvironment CompileEnvironment)
		{
			string Result = "";
			Result += " -x objective-c";
			Result += " -stdlib=libc++";
			return Result;
		}

		static string GetCompileArguments_C()
		{
			string Result = "";
			Result += " -x c";
			return Result;
		}

		static string GetCompileArguments_PCH(CppCompileEnvironment CompileEnvironment)
		{
			string Result = "";
			Result += " -x objective-c++-header";
			Result += GetCppStandardCompileArgument(CompileEnvironment);
			Result += " -stdlib=libc++";

			return Result;
		}

		// Conditionally enable (default disabled) generation of information about every class with virtual functions for use by the C++ runtime type identification features 
		// (`dynamic_cast' and `typeid'). If you don't use those parts of the language, you can save some space by using -fno-rtti. 
		// Note that exception handling uses the same information, but it will generate it as needed. 
		protected static string GetRTTIFlag(CppCompileEnvironment CompileEnvironment)
		{
			string Result = "";

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

	};
}
