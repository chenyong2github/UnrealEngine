using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

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
		public override int Execute(CommandLineArguments Arguments)
		{
			Arguments.ApplyTo(this);

			// Create the build configuration object, and read the settings
			BuildConfiguration BuildConfiguration = new BuildConfiguration();
			XmlConfig.ApplyTo(BuildConfiguration);
			Arguments.ApplyTo(BuildConfiguration);

			// Parse the filter argument
			FileFilter FileFilter = null;
			if(FilterRules.Count > 0)
			{
				FileFilter = new FileFilter(FileFilterType.Exclude);
				foreach(string FilterRule in FilterRules)
				{
					FileFilter.AddRules(FilterRule.Split(';'));
				}
			}

			// Parse all the target descriptors
			List<TargetDescriptor> TargetDescriptors = TargetDescriptor.ParseCommandLine(Arguments, BuildConfiguration.bUsePrecompiled, BuildConfiguration.bSkipRulesCompile);

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
					UEBuildTarget Target = UEBuildTarget.Create(TargetDescriptor, BuildConfiguration.bSkipRulesCompile, BuildConfiguration.bUsePrecompiled);

					// Find the location of the compiler
					VCEnvironment Environment = VCEnvironment.Create(WindowsCompiler.Clang, Target.Platform, Target.Rules.WindowsPlatform.Architecture, null, Target.Rules.WindowsPlatform.WindowsSdkVersion);
					FileReference ClangPath = FileReference.Combine(Environment.CompilerDir, "clang.exe");

					// Create all the binaries and modules
					CppCompileEnvironment GlobalCompileEnvironment = Target.CreateCompileEnvironmentForProjectFiles();
					foreach (UEBuildBinary Binary in Target.Binaries)
					{
						CppCompileEnvironment BinaryCompileEnvironment = Binary.CreateBinaryCompileEnvironment(GlobalCompileEnvironment);
						foreach (UEBuildModuleCPP Module in Binary.Modules.OfType<UEBuildModuleCPP>())
						{
							if(!Module.Rules.bUsePrecompiled)
							{
								UEBuildModuleCPP.InputFileCollection InputFileCollection = Module.FindInputFiles(Target.Platform, new Dictionary<DirectoryItem, FileItem[]>());

								List<FileItem> InputFiles = new List<FileItem>();
								InputFiles.AddRange(InputFileCollection.CPPFiles);
								InputFiles.AddRange(InputFileCollection.CCFiles);

								CppCompileEnvironment ModuleCompileEnvironment = Module.CreateModuleCompileEnvironment(Target.Rules, BinaryCompileEnvironment);

								StringBuilder CommandBuilder = new StringBuilder();
								CommandBuilder.AppendFormat("\"{0}\"", ClangPath.FullName);
								foreach (FileItem ForceIncludeFile in ModuleCompileEnvironment.ForceIncludeFiles)
								{
									CommandBuilder.AppendFormat(" -include \"{0}\"", ForceIncludeFile.FullName);
								}
								foreach (string Definition in ModuleCompileEnvironment.Definitions)
								{
									CommandBuilder.AppendFormat(" -D\"{0}\"", Definition);
								}
								foreach (DirectoryReference IncludePath in ModuleCompileEnvironment.UserIncludePaths)
								{
									CommandBuilder.AppendFormat(" -I\"{0}\"", IncludePath);
								}
								foreach (DirectoryReference IncludePath in ModuleCompileEnvironment.SystemIncludePaths)
								{
									CommandBuilder.AppendFormat(" -I\"{0}\"", IncludePath);
								}

								string Command = CommandBuilder.ToString();
								foreach (FileItem InputFile in InputFiles)
								{
									if(FileFilter == null || FileFilter.Matches(InputFile.Location.MakeRelativeTo(UnrealBuildTool.RootDirectory)))
									{
										FileToCommand[InputFile.Location] = Command;
									}
								}
							}
						}
					}
				}

				// Write the compile database
				FileReference DatabaseFile = FileReference.Combine(UnrealBuildTool.RootDirectory, "compile_commands.json");
				using (JsonWriter Writer = new JsonWriter(DatabaseFile))
				{
					Writer.WriteArrayStart();
					foreach(KeyValuePair<FileReference, string> FileCommandPair in FileToCommand.OrderBy(x => x.Key.FullName))
					{
						Writer.WriteObjectStart();
						Writer.WriteValue("file", FileCommandPair.Key.FullName);
						Writer.WriteValue("command", FileCommandPair.Value);
						Writer.WriteValue("directory", UnrealBuildTool.EngineSourceDirectory.ToString());
						Writer.WriteObjectEnd();
					}
					Writer.WriteArrayEnd();
				}
			}

			return 0;
		}
	}
}
