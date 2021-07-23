// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	abstract class UEToolChain
	{
		public UEToolChain()
		{
		}

		public virtual void SetEnvironmentVariables()
		{
		}

		public virtual void GetVersionInfo(List<string> Lines)
		{
		}

		public virtual void GetExternalDependencies(HashSet<FileItem> ExternalDependencies)
		{
		}

		public abstract CPPOutput CompileCPPFiles(CppCompileEnvironment CompileEnvironment, List<FileItem> InputFiles, DirectoryReference OutputDir, string ModuleName, IActionGraphBuilder Graph);

		public virtual CPPOutput CompileRCFiles(CppCompileEnvironment Environment, List<FileItem> InputFiles, DirectoryReference OutputDir, IActionGraphBuilder Graph)
		{
			CPPOutput Result = new CPPOutput();
			return Result;
		}

		public virtual CPPOutput CompileISPCFiles(CppCompileEnvironment Environment, List<FileItem> InputFiles, DirectoryReference OutputDir, IActionGraphBuilder Graph)
		{
			CPPOutput Result = new CPPOutput();
			return Result;
		}
		public virtual CPPOutput GenerateISPCHeaders(CppCompileEnvironment Environment, List<FileItem> InputFiles, DirectoryReference OutputDir, IActionGraphBuilder Graph)
		{
			CPPOutput Result = new CPPOutput();
			return Result;
		}

		public virtual void GenerateTypeLibraryHeader(CppCompileEnvironment CompileEnvironment, ModuleRules.TypeLibrary TypeLibrary, FileReference OutputFile, IActionGraphBuilder Graph)
		{
			throw new NotSupportedException("This platform does not support type libraries.");
		}

		public abstract FileItem LinkFiles(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly, IActionGraphBuilder Graph);
		public virtual FileItem[] LinkAllFiles(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly, IActionGraphBuilder Graph)
		{
			return new FileItem[] { LinkFiles(LinkEnvironment, bBuildImportLibraryOnly, Graph) };
		}


		/// <summary>
		/// Get the name of the response file for the current linker environment and output file
		/// </summary>
		/// <param name="LinkEnvironment"></param>
		/// <param name="OutputFile"></param>
		/// <returns></returns>
		public static FileReference GetResponseFileName(LinkEnvironment LinkEnvironment, FileItem OutputFile)
		{
			// Construct a relative path for the intermediate response file
			return FileReference.Combine(LinkEnvironment.IntermediateDirectory, OutputFile.Location.GetFileName() + ".response");
		}

		public virtual ICollection<FileItem> PostBuild(FileItem Executable, LinkEnvironment ExecutableLinkEnvironment, IActionGraphBuilder Graph)
		{
			return new List<FileItem>();
		}

		public virtual void SetUpGlobalEnvironment(ReadOnlyTargetRules Target)
		{
		}

		public virtual void ModifyBuildProducts(ReadOnlyTargetRules Target, UEBuildBinary Binary, List<string> Libraries, List<UEBuildBundleResource> BundleResources, Dictionary<FileReference, BuildProductType> BuildProducts)
		{
		}

		public virtual void FinalizeOutput(ReadOnlyTargetRules Target, TargetMakefile Makefile)
		{
		}

		public virtual void PrepareRuntimeDependencies( List<RuntimeDependency> RuntimeDependencies, Dictionary<FileReference, FileReference> TargetFileToSourceFile, DirectoryReference ExeDir )
		{
			
		}

		/// <summary>
		/// Adds a build product and its associated debug file to a receipt.
		/// </summary>
		/// <param name="OutputFile">Build product to add</param>
		/// <param name="OutputType">The type of build product</param>
		public virtual bool ShouldAddDebugFileToReceipt(FileReference OutputFile, BuildProductType OutputType)
		{
			return true;
		}
		
		public virtual FileReference GetDebugFile(FileReference OutputFile, string DebugExtension)
		{
			//  by default, just change the extension to the debug extension
			return OutputFile.ChangeExtension(DebugExtension);
		}


		public virtual void SetupBundleDependencies(List<UEBuildBinary> Binaries, string GameName)
		{

		}

        public virtual string GetSDKVersion()
        {
            return "Not Applicable";
        }

		/// <summary>
		/// Runs the provided tool and argument. Returns the output, using a rexex capture if one is provided
		/// </summary>
		/// <param name="Command">Full path to the tool to run</param>
		/// <param name="ToolArg">Argument that will be passed to the tool</param>
		/// <param name="Expression">null, or a Regular expression to capture in the output</param>
		/// <returns></returns>
		public static string RunToolAndCaptureOutput(FileReference Command, string ToolArg, string Expression = null)
		{
			string ProcessOutput = Utils.RunLocalProcessAndReturnStdOut(Command.FullName, ToolArg);

			if (string.IsNullOrEmpty(Expression))
			{
				return ProcessOutput;
			}
			
			Match M = Regex.Match(ProcessOutput, Expression);
			return M.Success ? M.Groups[1].ToString() : null;	
		}

		/// <summary>
		/// Runs the provided tool and argument and parses the output to retrieve the version
		/// </summary>
		/// <param name="Command">Full path to the tool to run</param>
		/// <param name="VersionArg">Argument that will result in the version string being shown (it's ok this is a byproduct of a command that returns an error)</param>
		/// <param name="VersionExpression">Regular expression to capture the version. By default we look for four integers separated by periods, with the last two optional</param>
		/// <returns></returns>
		public static Version RunToolAndCaptureVersion(FileReference Command, string VersionArg, string VersionExpression = @"(\d+\.\d+(\.\d+)?(\.\d+)?)")
		{
			string ProcessOutput = RunToolAndCaptureOutput(Command, VersionArg, VersionExpression);

			Version ToolVersion = new Version(0, 0);

			if (Version.TryParse(ProcessOutput, out ToolVersion))
			{
				return ToolVersion;
			}

			Log.TraceWarning("Unable to retrieve version from {0} {1}", Command, VersionArg);

			return ToolVersion;
		}
	};

	abstract class ISPCToolChain : UEToolChain
	{
		/// <summary>
		/// Get CPU Instruction set targets for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS platform to target.</param>
		/// <param name="Arch">Which architecture inside an OS platform to target. Only used for Android currently.</param>
		/// <returns>List of instruction set targets passed to ISPC compiler</returns>
		public virtual List<string> GetISPCCompileTargets(UnrealTargetPlatform Platform, string Arch)
		{
			List<string> ISPCTargets = new List<string>();

			if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Windows) ||
				(UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Unix) && Platform != UnrealTargetPlatform.LinuxAArch64) ||
				Platform == UnrealTargetPlatform.Mac)
			{
				ISPCTargets.AddRange(new string[] { "avx512skx-i32x8", "avx2", "avx", "sse4", "sse2" });
			}
			else if (Platform == UnrealTargetPlatform.LinuxAArch64)
			{
				ISPCTargets.AddRange(new string[] { "neon" });
			}
			else if (Platform == UnrealTargetPlatform.Android || Platform == UnrealTargetPlatform.Lumin)
			{
				switch (Arch)
				{
					case "-armv7": ISPCTargets.Add("neon"); break; // Assumes NEON is in use
					case "-arm64": ISPCTargets.Add("neon"); break;
					case "-x86": ISPCTargets.AddRange(new string[] { "sse4", "sse2" }); break;
					case "-x64": ISPCTargets.AddRange(new string[] { "sse4", "sse2" }); break;
					default: Log.TraceWarning("Invalid Android architecture for ISPC. At least one architecture (armv7, x86, etc) needs to be selected in the project settings to build"); break;
				}
			}
			else
			{
				Log.TraceWarning("Unsupported ISPC platform target!");
			}

			return ISPCTargets;
		}

		/// <summary>
		/// Get OS target for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS platform to target.</param>
		/// <returns>OS string passed to ISPC compiler</returns>
		public virtual string GetISPCOSTarget(UnrealTargetPlatform Platform)
		{
			string ISPCOS = "";

			if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Windows))
			{
				ISPCOS += "windows";
			}
			else if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Unix))
			{
				ISPCOS += "linux";
			}
			else if (Platform == UnrealTargetPlatform.Android || Platform == UnrealTargetPlatform.Lumin)
			{
				ISPCOS += "android";
			}
			else if (Platform == UnrealTargetPlatform.Mac)
			{
				ISPCOS += "macos";
			}
			else
			{
				Log.TraceWarning("Unsupported ISPC platform target!");
			}

			return ISPCOS;
		}

		/// <summary>
		/// Get CPU architecture target for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS platform to target.</param>
		/// <param name="Arch">Which architecture inside an OS platform to target. Only used for Android currently.</param>
		/// <returns>Arch string passed to ISPC compiler</returns>
		public virtual string GetISPCArchTarget(UnrealTargetPlatform Platform, string Arch)
		{
			string ISPCArch = "";

			if ((UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Windows) && Platform != UnrealTargetPlatform.Win32) ||
				(UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Unix) && Platform != UnrealTargetPlatform.LinuxAArch64) ||
				Platform == UnrealTargetPlatform.Mac)
			{
				ISPCArch += "x86-64";
			}
			else if (Platform == UnrealTargetPlatform.Win32)
			{
				ISPCArch += "x86";
			}
			else if (Platform == UnrealTargetPlatform.LinuxAArch64)
			{
				ISPCArch += "aarch64";
			}
			else if (Platform == UnrealTargetPlatform.Android || Platform == UnrealTargetPlatform.Lumin)
			{
				switch (Arch)
				{
					case "-armv7": ISPCArch += "arm"; break; // Assumes NEON is in use
					case "-arm64": ISPCArch += "aarch64"; break;
					case "-x86": ISPCArch += "x86"; break;
					case "-x64": ISPCArch += "x86-64"; break;
					default: Log.TraceWarning("Invalid Android architecture for ISPC. At least one architecture (armv7, x86, etc) needs to be selected in the project settings to build"); break;
				}
			}
			else
			{
				Log.TraceWarning("Unsupported ISPC platform target!");
			}

			return ISPCArch;
		}

		/// <summary>
		/// Get host compiler path for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS build platform is running on.</param>
		/// <returns>Path to ISPC compiler</returns>
		public virtual string GetISPCHostCompilerPath(UnrealTargetPlatform Platform)
		{
			string ISPCCompilerPathCommon = Path.Combine(UnrealBuildTool.EngineSourceDirectory.FullName, "ThirdParty", "Intel", "ISPC", "bin");
			string ISPCArchitecturePath = "";
			string ExeExtension = ".exe";

			if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Windows))
			{
				ISPCArchitecturePath = "Windows";
			}
			else if (Platform == UnrealTargetPlatform.Linux)
			{
				ISPCArchitecturePath = "Linux";
				ExeExtension = "";
			}
			else if (Platform == UnrealTargetPlatform.Mac)
			{
				ISPCArchitecturePath = "Mac";
				ExeExtension = "";
			}
			else
			{
				Log.TraceWarning("Unsupported ISPC host!");
			}

			return Path.Combine(ISPCCompilerPathCommon, ISPCArchitecturePath, "ispc" + ExeExtension);
		}

		static Dictionary<UnrealTargetPlatform, string> ISPCCompilerVersions = new Dictionary<UnrealTargetPlatform, string>();

		/// <summary>
		/// Returns the version of the ISPC compiler for the specified platform. If GetISPCHostCompilerPath() doesn't return a valid path
		/// this will return a -1 version.
		/// </summary>
		/// <param name="Platform">Which OS build platform is running on.</param>
		/// <returns>Version reported by the ISPC compilerr</returns>
		public virtual string GetISPCHostCompilerVersion(UnrealTargetPlatform Platform)
		{
			if (!ISPCCompilerVersions.ContainsKey(Platform))
			{
				Version CompilerVersion = null;
				string CompilerPath = GetISPCHostCompilerPath(Platform);

				if (!File.Exists(CompilerPath))
				{
					Log.TraceWarning("No ISPC compiler at {0}", CompilerPath);
					CompilerVersion = new Version(-1, -1);
				}

				ISPCCompilerVersions[Platform] = RunToolAndCaptureOutput(new FileReference(CompilerPath), "--version", "(.*)");
			}

			return ISPCCompilerVersions[Platform];			
		}

		/// <summary>
		/// Get object file suffix for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS build platform is running on.</param>
		/// <returns>Object file suffix</returns>
		public virtual string GetISPCObjectFileSuffix(UnrealTargetPlatform Platform)
		{
			string Suffix = "";

			if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Windows))
			{
				Suffix += ".obj";
			}
			else if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Unix) ||
					Platform == UnrealTargetPlatform.Mac ||
					Platform == UnrealTargetPlatform.Android ||
					Platform == UnrealTargetPlatform.Lumin)
			{
				Suffix += ".o";
			}
			else
			{
				Log.TraceWarning("Unsupported ISPC platform target!");
			}

			return Suffix;
		}

		public override CPPOutput GenerateISPCHeaders(CppCompileEnvironment CompileEnvironment, List<FileItem> InputFiles, DirectoryReference OutputDir, IActionGraphBuilder Graph)
		{
			CPPOutput Result = new CPPOutput();

			if(!CompileEnvironment.bCompileISPC)
			{
				return Result;
			}

			List<string> CompileTargets = GetISPCCompileTargets(CompileEnvironment.Platform, null);

			foreach (FileItem ISPCFile in InputFiles)
			{
				Action CompileAction = Graph.CreateAction(ActionType.Compile);
				CompileAction.CommandDescription = "Compile";
				CompileAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
				CompileAction.CommandPath = new FileReference(GetISPCHostCompilerPath(BuildHostPlatform.Current.Platform));
				CompileAction.StatusDescription = Path.GetFileName(ISPCFile.AbsolutePath);

				// Disable remote execution to workaround mismatched case on XGE
				CompileAction.bCanExecuteRemotely = false;

				List<string> Arguments = new List<string>();

				// Add the ISPC obj file as a prerequisite of the action.
				CompileAction.CommandArguments = String.Format("\"{0}\" ", ISPCFile.AbsolutePath);

				// Add the ISPC h file to the produced item list.
				FileItem ISPCIncludeHeaderFile = FileItem.GetItemByFileReference(
					FileReference.Combine(
						OutputDir,
						Path.GetFileName(ISPCFile.AbsolutePath) + ".generated.dummy.h"
						)
					);

				// Add the ISPC file to be compiled.
				Arguments.Add(String.Format("-h \"{0}\"", ISPCIncludeHeaderFile));

				// Build target string. No comma on last
				string TargetString = "";
				foreach (string Target in CompileTargets)
				{
					if (Target == CompileTargets[CompileTargets.Count-1]) // .Last()
					{
						TargetString += Target;
					}
					else
					{
						TargetString += Target + ",";
					}
				}

				// Build target triplet
				Arguments.Add(String.Format("--target-os={0}", GetISPCOSTarget(CompileEnvironment.Platform)));
				Arguments.Add(String.Format("--arch={0}", GetISPCArchTarget(CompileEnvironment.Platform, null)));
				Arguments.Add(String.Format("--target={0}", TargetString));

				// PIC is needed for modular builds except on Windows
				if ((CompileEnvironment.bIsBuildingDLL ||
					CompileEnvironment.bIsBuildingLibrary) &&
					!UEBuildPlatform.IsPlatformInGroup(CompileEnvironment.Platform, UnrealPlatformGroup.Windows))
				{
					Arguments.Add("--pic");
				}

				// Include paths. Don't use AddIncludePath() here, since it uses the full path and exceeds the max command line length.
				// Because ISPC response files don't support white space in arguments, paths with white space need to be passed to the command line directly.
				foreach (DirectoryReference IncludePath in CompileEnvironment.UserIncludePaths)
				{
					Arguments.Add(String.Format("-I\"{0}\"", IncludePath));
				}

				// System include paths.
				foreach (DirectoryReference SystemIncludePath in CompileEnvironment.SystemIncludePaths)
				{
					Arguments.Add(String.Format("-I\"{0}\"", SystemIncludePath));
				}

				// Generate the included header dependency list
				if (CompileEnvironment.bGenerateDependenciesFile)
				{
					FileItem DependencyListFile = FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, Path.GetFileName(ISPCFile.AbsolutePath) + ".txt"));
					Arguments.Add(String.Format("-MMM \"{0}\"", DependencyListFile.AbsolutePath.Replace('\\', '/')));
					CompileAction.DependencyListFile = DependencyListFile;
					CompileAction.ProducedItems.Add(DependencyListFile);
				}

				CompileAction.ProducedItems.Add(ISPCIncludeHeaderFile);

				FileReference ResponseFileName = new FileReference(ISPCIncludeHeaderFile.AbsolutePath + ".response");
				FileItem ResponseFileItem = Graph.CreateIntermediateTextFile(ResponseFileName, Arguments.Select(x => Utils.ExpandVariables(x)));
				CompileAction.CommandArguments += String.Format("@\"{0}\"", ResponseFileName);
				CompileAction.PrerequisiteItems.Add(ResponseFileItem);

				// Add the source file and its included files to the prerequisite item list.
				CompileAction.PrerequisiteItems.Add(ISPCFile);

				FileItem ISPCFinalHeaderFile = FileItem.GetItemByFileReference(
					FileReference.Combine(
						OutputDir,
						Path.GetFileName(ISPCFile.AbsolutePath) + ".generated.h"
						)
					);

				// Fix interrupted build issue by copying header after generation completes
				FileReference SourceFile = ISPCIncludeHeaderFile.Location;
				FileReference TargetFile = ISPCFinalHeaderFile.Location;

				FileItem SourceFileItem = FileItem.GetItemByFileReference(SourceFile);
				FileItem TargetFileItem = FileItem.GetItemByFileReference(TargetFile);

				Action CopyAction = Graph.CreateAction(ActionType.BuildProject);
				CopyAction.CommandDescription = "Copy";
				CopyAction.CommandPath = BuildHostPlatform.Current.Shell;
				if (BuildHostPlatform.Current.ShellType == ShellType.Cmd)
				{
					CopyAction.CommandArguments = String.Format("/C \"copy /Y \"{0}\" \"{1}\" 1>nul\"", SourceFile, TargetFile);
				}
				else
				{
					CopyAction.CommandArguments = String.Format("-c 'cp -f \"{0}\" \"{1}\"'", SourceFile.FullName, TargetFile.FullName);
				}
				CopyAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
				CopyAction.PrerequisiteItems.Add(SourceFileItem);
				CopyAction.ProducedItems.Add(TargetFileItem);
				CopyAction.StatusDescription = TargetFileItem.Location.GetFileName();
				CopyAction.bCanExecuteRemotely = false;
				CopyAction.bShouldOutputStatusDescription = false;

				Result.GeneratedHeaderFiles.Add(TargetFileItem);

				Log.TraceVerbose("   ISPC Generating Header " + CompileAction.StatusDescription + ": \"" + CompileAction.CommandPath + "\"" + CompileAction.CommandArguments);
			}

			return Result;
		}

		public override CPPOutput CompileISPCFiles(CppCompileEnvironment CompileEnvironment, List<FileItem> InputFiles, DirectoryReference OutputDir, IActionGraphBuilder Graph)
		{
			CPPOutput Result = new CPPOutput();

			if (!CompileEnvironment.bCompileISPC)
			{
				return Result;
			}

			List<string> CompileTargets = GetISPCCompileTargets(CompileEnvironment.Platform, null);

			foreach (FileItem ISPCFile in InputFiles)
			{
				Action CompileAction = Graph.CreateAction(ActionType.Compile);
				CompileAction.CommandDescription = "Compile";
				CompileAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
				CompileAction.CommandPath = new FileReference(GetISPCHostCompilerPath(BuildHostPlatform.Current.Platform));
				CompileAction.StatusDescription = Path.GetFileName(ISPCFile.AbsolutePath);

				// Disable remote execution to workaround mismatched case on XGE
				CompileAction.bCanExecuteRemotely = false;

				List<string> Arguments = new List<string>();

				// Add the ISPC file to be compiled.
				Arguments.Add(String.Format(" \"{0}\"", ISPCFile.AbsolutePath));

				List<FileItem> CompiledISPCObjFiles = new List<FileItem>();
				string TargetString = "";

				foreach (string Target in CompileTargets)
				{
					string ObjTarget = Target;

					if (Target.Contains("-"))
					{
						// Remove lane width and gang size from obj file name
						ObjTarget = Target.Split('-')[0];
					}

					FileItem CompiledISPCObjFile;

					if (CompileTargets.Count > 1)
					{
						CompiledISPCObjFile = FileItem.GetItemByFileReference(
						FileReference.Combine(
							OutputDir,
							Path.GetFileName(ISPCFile.AbsolutePath) + "_" + ObjTarget + GetISPCObjectFileSuffix(CompileEnvironment.Platform)
							)
						);
					}
					else
					{
						CompiledISPCObjFile = FileItem.GetItemByFileReference(
						FileReference.Combine(
							OutputDir,
							Path.GetFileName(ISPCFile.AbsolutePath) + GetISPCObjectFileSuffix(CompileEnvironment.Platform)
							)
						);
					}

					// Add the ISA specific ISPC obj files to the produced item list.
					CompiledISPCObjFiles.Add(CompiledISPCObjFile);

					// Build target string. No comma on last
					if (Target == CompileTargets[CompileTargets.Count-1]) // .Last()
					{
						TargetString += Target;
					}
					else
					{
						TargetString += Target + ",";
					}
				}

				// Add the common ISPC obj file to the produced item list.
				FileItem CompiledISPCObjFileNoISA = FileItem.GetItemByFileReference(
					FileReference.Combine(
						OutputDir,
						Path.GetFileName(ISPCFile.AbsolutePath) + GetISPCObjectFileSuffix(CompileEnvironment.Platform)
						)
					);

				CompiledISPCObjFiles.Add(CompiledISPCObjFileNoISA);

				// Add the output ISPC obj file
				Arguments.Add(String.Format("-o \"{0}\"", CompiledISPCObjFileNoISA));

				// Build target triplet
				Arguments.Add(String.Format("--target-os=\"{0}\"", GetISPCOSTarget(CompileEnvironment.Platform)));
				Arguments.Add(String.Format("--arch=\"{0}\"", GetISPCArchTarget(CompileEnvironment.Platform, null)));
				Arguments.Add(String.Format("--target=\"{0}\"", TargetString));

				if (CompileEnvironment.Configuration == CppConfiguration.Debug)
				{
					if (CompileEnvironment.Platform == UnrealTargetPlatform.Mac)
					{
						// Turn off debug symbols on Mac due to dsym generation issue
						Arguments.Add("-O0");
						// Ideally we would be able to turn on symbols and specify the dwarf version, but that does
						// does not seem to be working currently, ie:
						//    Arguments.Add("-g -O0 --dwarf-version=2");

					}
					else
					{
						Arguments.Add("-g -O0");
					}
				}
				else
				{
					Arguments.Add("-O2");
				}

				// PIC is needed for modular builds except on Windows
				if ((CompileEnvironment.bIsBuildingDLL || 
					CompileEnvironment.bIsBuildingLibrary) &&
					!UEBuildPlatform.IsPlatformInGroup(CompileEnvironment.Platform, UnrealPlatformGroup.Windows))
				{
					Arguments.Add("--pic");
				}

				// Include paths. Don't use AddIncludePath() here, since it uses the full path and exceeds the max command line length.
				foreach (DirectoryReference IncludePath in CompileEnvironment.UserIncludePaths)
				{
					Arguments.Add(String.Format("-I\"{0}\"", IncludePath));
				}

				// System include paths.
				foreach (DirectoryReference SystemIncludePath in CompileEnvironment.SystemIncludePaths)
				{
					Arguments.Add(String.Format("-I\"{0}\"", SystemIncludePath));
				}

				// Preprocessor definitions.
				foreach (string Definition in CompileEnvironment.Definitions)
				{
					Arguments.Add(String.Format("-D\"{0}\"", Definition));
				}

				// Consume the included header dependency list
				if (CompileEnvironment.bGenerateDependenciesFile)
				{
					FileItem DependencyListFile = FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, Path.GetFileName(ISPCFile.AbsolutePath) + ".txt"));
					CompileAction.DependencyListFile = DependencyListFile;
					CompileAction.PrerequisiteItems.Add(DependencyListFile);
				}

				CompileAction.ProducedItems.AddRange(CompiledISPCObjFiles);
				Result.ObjectFiles.AddRange(CompiledISPCObjFiles);

				FileReference ResponseFileName = new FileReference(CompiledISPCObjFileNoISA.AbsolutePath + ".response");
				FileItem ResponseFileItem = Graph.CreateIntermediateTextFile(ResponseFileName, Arguments.Select(x => Utils.ExpandVariables(x)));
				CompileAction.CommandArguments = " @\"" + ResponseFileName + "\"";
				CompileAction.PrerequisiteItems.Add(ResponseFileItem);

				// Add the source file and its included files to the prerequisite item list.
				CompileAction.PrerequisiteItems.Add(ISPCFile);

				Log.TraceVerbose("   ISPC Compiling " + CompileAction.StatusDescription + ": \"" + CompileAction.CommandPath + "\"" + CompileAction.CommandArguments);
			}

			return Result;
		}
	}
}
