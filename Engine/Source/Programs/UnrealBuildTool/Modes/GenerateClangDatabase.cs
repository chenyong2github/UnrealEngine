// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using UnrealBuildBase;
using Microsoft.Extensions.Logging;

namespace UnrealBuildTool
{
	/// <summary>
	/// Generate a clang compile_commands file for a target
	/// </summary>
	[ToolMode("GenerateClangDatabase", ToolModeOptions.XmlConfig | ToolModeOptions.BuildPlatforms | ToolModeOptions.SingleInstance | ToolModeOptions.StartPrefetchingEngine | ToolModeOptions.ShowExecutionTime)]
	class GenerateClangDatabase : ToolMode
	{
		/// <summary>
		/// Set of filters for files to include in the database. Relative to the root directory, or to the project file.
		/// </summary>
		[CommandLine("-Filter=")]
		List<string> FilterRules = new List<string>();

		/// <summary>
		/// Execute the command
		/// </summary>
		/// <param name="Arguments">Command line arguments</param>
		/// <returns>Exit code</returns>
		/// <param name="Logger"></param>
		public override int Execute(CommandLineArguments Arguments, ILogger Logger)
		{
			Arguments.ApplyTo(this);

			// Create the build configuration object, and read the settings
			BuildConfiguration BuildConfiguration = new BuildConfiguration();
			XmlConfig.ApplyTo(BuildConfiguration);
			Arguments.ApplyTo(BuildConfiguration);

			// Parse the filter argument
			FileFilter? FileFilter = null;
			if (FilterRules.Count > 0)
			{
				FileFilter = new FileFilter(FileFilterType.Exclude);
				foreach (string FilterRule in FilterRules)
				{
					FileFilter.AddRules(FilterRule.Split(';'));
				}
			}

			// Force C++ modules to always include their generated code directories
			UEBuildModuleCPP.bForceAddGeneratedCodeIncludePath = true;

			// Parse all the target descriptors
			List<TargetDescriptor> TargetDescriptors = TargetDescriptor.ParseCommandLine(Arguments, BuildConfiguration.bUsePrecompiled, BuildConfiguration.bSkipRulesCompile, BuildConfiguration.bForceRulesCompile, Logger);

			// Generate the compile DB for each target
			using (ISourceFileWorkingSet WorkingSet = new EmptySourceFileWorkingSet())
			{
				// Find the compile commands for each file in the target
				Dictionary<FileReference, string> FileToCommand = new Dictionary<FileReference, string>();
				foreach (TargetDescriptor TargetDescriptor in TargetDescriptors)
				{
					// Disable PCHs and unity builds for the target
					TargetDescriptor.AdditionalArguments = TargetDescriptor.AdditionalArguments.Append(new string[] { "-NoPCH", "-DisableUnity" });

					// Create a makefile for the target
					UEBuildTarget Target = UEBuildTarget.Create(TargetDescriptor, BuildConfiguration.bSkipRulesCompile, BuildConfiguration.bForceRulesCompile, BuildConfiguration.bUsePrecompiled, Logger);

					// Find the location of the compiler
					FileReference ClangPath = FindClangCompiler(Target, Logger);

					bool IsWindowsClang = ClangPath.GetFileName().Equals("clang-cl.exe", StringComparison.OrdinalIgnoreCase);

					// Create all the binaries and modules
					CppCompileEnvironment GlobalCompileEnvironment = Target.CreateCompileEnvironmentForProjectFiles(Logger);
					foreach (UEBuildBinary Binary in Target.Binaries)
					{
						CppCompileEnvironment BinaryCompileEnvironment = Binary.CreateBinaryCompileEnvironment(GlobalCompileEnvironment);
						foreach (UEBuildModuleCPP Module in Binary.Modules.OfType<UEBuildModuleCPP>())
						{
							if (!Module.Rules.bUsePrecompiled)
							{
								UEBuildModuleCPP.InputFileCollection InputFileCollection = Module.FindInputFiles(Target.Platform, new Dictionary<DirectoryItem, FileItem[]>());

								CppCompileEnvironment ModuleCompileEnvironment = Module.CreateModuleCompileEnvironment(Target.Rules, BinaryCompileEnvironment);

								StringBuilder CommandBuilder = new StringBuilder();
								StringBuilder CppCommandBuilder = new StringBuilder();
								StringBuilder CCommandBuilder = new StringBuilder();
								CommandBuilder.AppendFormat("\"{0}\"", ClangPath.FullName);

								switch (ModuleCompileEnvironment.CppStandard)
								{
									case CppStandardVersion.Cpp14:
										CppCommandBuilder.AppendFormat(IsWindowsClang ? " /std:c++14" : " -std=c++14");
										break;
									case CppStandardVersion.Cpp17:
										CppCommandBuilder.AppendFormat(IsWindowsClang ? " /std:c++17" : " -std=c++17");
										break;
									case CppStandardVersion.Cpp20:
									case CppStandardVersion.Latest:
										CppCommandBuilder.AppendFormat(IsWindowsClang ? " /std:c++latest" : " -std=c++20");
										break;
									default:
										throw new BuildException($"Unsupported C++ standard type set: {ModuleCompileEnvironment.CppStandard}");
								}

								if (ModuleCompileEnvironment.bEnableCoroutines)
								{
									CppCommandBuilder.AppendFormat(" -fcoroutines-ts");
									if (!ModuleCompileEnvironment.bEnableExceptions)
									{
										CppCommandBuilder.AppendFormat(" -Wno-coroutine-missing-unhandled-exception");
									}
								}

								switch (ModuleCompileEnvironment.CStandard)
								{
									case CStandardVersion.Default:
										break;
									case CStandardVersion.C89:
										CCommandBuilder.AppendFormat(IsWindowsClang ? "" : " -std=c89");
										break;
									case CStandardVersion.C99:
										CCommandBuilder.AppendFormat(IsWindowsClang ? "" : " -std=c99");
										break;
									case CStandardVersion.C11:
										CCommandBuilder.AppendFormat(IsWindowsClang ? " /std:c11" : " -std=c11");
										break;
									case CStandardVersion.C17:
										CCommandBuilder.AppendFormat(IsWindowsClang ? " /std:c17" : " -std=c17");
										break;
									case CStandardVersion.Latest:
										CCommandBuilder.AppendFormat(IsWindowsClang ? " /std:c17" : " -std=c2x");
										break;
									default:
										throw new BuildException($"Unsupported C standard type set: {ModuleCompileEnvironment.CStandard}");
								}

								foreach (FileItem ForceIncludeFile in ModuleCompileEnvironment.ForceIncludeFiles)
								{
									CommandBuilder.AppendFormat(" -include \"{0}\"", ForceIncludeFile.FullName);
								}
								foreach (string Definition in ModuleCompileEnvironment.Definitions)
								{
									CommandBuilder.AppendFormat(" -D\"{0}\"", Definition.Replace("\"", "\\\""));
								}
								foreach (DirectoryReference IncludePath in ModuleCompileEnvironment.UserIncludePaths)
								{
									CommandBuilder.AppendFormat(" -I\"{0}\"", IncludePath);
								}
								foreach (DirectoryReference IncludePath in ModuleCompileEnvironment.SystemIncludePaths)
								{
									CommandBuilder.AppendFormat(" -I\"{0}\"", IncludePath);
								}

								foreach (FileItem InputFile in InputFileCollection.CPPFiles)
								{
									if (FileFilter == null || FileFilter.Matches(InputFile.Location.MakeRelativeTo(Unreal.RootDirectory)))
									{
										FileToCommand[InputFile.Location] = String.Format("{0} {1} \"{2}\"", CommandBuilder, CppCommandBuilder, InputFile.FullName);
									}
								}

								foreach (FileItem InputFile in InputFileCollection.CFiles)
								{
									if (FileFilter == null || FileFilter.Matches(InputFile.Location.MakeRelativeTo(Unreal.RootDirectory)))
									{
										FileToCommand[InputFile.Location] = String.Format("{0} {1} \"{2}\"", CommandBuilder, CCommandBuilder, InputFile.FullName);
									}
								}
							}
						}
					}
				}

				// Write the compile database
				DirectoryReference DatabaseDirectory = Arguments.GetDirectoryReferenceOrDefault("-OutputDir=", Unreal.RootDirectory);
				FileReference DatabaseFile = FileReference.Combine(DatabaseDirectory, "compile_commands.json");
				using (JsonWriter Writer = new JsonWriter(DatabaseFile))
				{
					Writer.WriteArrayStart();
					foreach (KeyValuePair<FileReference, string> FileCommandPair in FileToCommand.OrderBy(x => x.Key.FullName))
					{
						Writer.WriteObjectStart();
						Writer.WriteValue("file", FileCommandPair.Key.FullName);
						Writer.WriteValue("command", FileCommandPair.Value);
						Writer.WriteValue("directory", Unreal.EngineSourceDirectory.ToString());
						Writer.WriteObjectEnd();
					}
					Writer.WriteArrayEnd();
				}
			}

			return 0;
		}

		/// <summary>
		/// Searches for the Clang compiler for the given platform.
		/// </summary>
		/// <param name="Target">The build platform to use to search for the Clang compiler.</param>
		/// <param name="Logger">Logger for output</param>
		/// <returns>The path to the Clang compiler.</returns>
		private static FileReference FindClangCompiler(UEBuildTarget Target, ILogger Logger)
		{
			UnrealTargetPlatform HostPlatform = BuildHostPlatform.Current.Platform;

			if (OperatingSystem.IsWindows())
			{
				VCEnvironment Environment = VCEnvironment.Create(WindowsCompiler.Clang, Target.Platform,
					Target.Rules.WindowsPlatform.Architecture, null,
					Target.Rules.WindowsPlatform.WindowsSdkVersion, null,
					Logger);

				return Environment.CompilerPath;
			}
			else if (OperatingSystem.IsLinux())
			{
				string? Clang = LinuxCommon.WhichClang(Logger);

				if (Clang != null)
				{
					return FileReference.FromString(Clang);
				}
			}
			else if (OperatingSystem.IsMacOS())
			{
				MacToolChainSettings Settings = new MacToolChainSettings(false, Logger);
				DirectoryReference? ToolchainDir = DirectoryReference.FromString(Settings.ToolchainDir);

				if (ToolchainDir != null)
				{
					return FileReference.Combine(ToolchainDir, "clang++");
				}
			}
			return FileReference.FromString("clang++");
		}
	}
}
