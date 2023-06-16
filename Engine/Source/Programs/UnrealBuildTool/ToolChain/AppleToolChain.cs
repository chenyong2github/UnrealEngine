// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	abstract class AppleToolChainSettings
	{
		/// <summary>
		/// Which developer directory to root from? If this is "xcode-select", UBT will query for the currently selected Xcode
		/// </summary>
		public string XcodeDeveloperDir = "xcode-select";

		public AppleToolChainSettings(bool bVerbose, ILogger Logger)
		{
			SelectXcode(ref XcodeDeveloperDir, bVerbose, Logger);
		}

		private static void SelectXcode(ref string DeveloperDir, bool bVerbose, ILogger Logger)
		{
			string Reason = "hardcoded";

			if (DeveloperDir == "xcode-select")
			{
				Reason = "xcode-select";

				// on the Mac, run xcode-select directly.
				int ReturnCode;
				DeveloperDir = Utils.RunLocalProcessAndReturnStdOut("xcode-select", "--print-path", null, out ReturnCode);
				if (ReturnCode != 0)
				{
					string? MinVersion = UEBuildPlatform.GetSDK(UnrealTargetPlatform.Mac)!.GetSDKInfo("Sdk")!.Min;
					throw new BuildException($"We were unable to find your build tools (via 'xcode-select --print-path'). Please install Xcode, version {MinVersion} or later");
				}

				// make sure we get a full path
				if (Directory.Exists(DeveloperDir) == false)
				{
					throw new BuildException("Selected Xcode ('{0}') doesn't exist, cannot continue.", DeveloperDir);
				}

				if (DeveloperDir.Contains("CommandLineTools", StringComparison.InvariantCultureIgnoreCase))
				{
					throw new BuildException($"Your Mac is set to use CommandLineTools for its build tools ({DeveloperDir}). Unreal expects Xcode as the build tools. Please install Xcode if it's not already, then do one of the following:\n" +
						"  - Run Xcode, go to Settings, and in the Locations tab, choose your Xcode in Command Line Tools dropdown.\n" +
						"  - In Terminal, run 'sudo xcode-select -s /Applications/Xcode.app' (or an alternate location if you installed Xcode to a non-standard location)\n" +
						"Either way, you will need to enter your Mac password.");
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

			// Installed engine requires Xcode 13
			if (Unreal.IsEngineInstalled())
			{
				string? InstalledSdkVersion = UnrealBuildBase.ApplePlatformSDK.InstalledSDKVersion;
				if (String.IsNullOrEmpty(InstalledSdkVersion))
				{
					throw new BuildException("Unable to get xcode version");
				}
				if (Int32.Parse(InstalledSdkVersion.Substring(0, 2)) < 13)
				{
					throw new BuildException("Building for macOS, iOS and tvOS requires Xcode 13.4.1 or newer, Xcode " + InstalledSdkVersion + " detected");
				}
			}
		}

		protected void SelectSDK(string BaseSDKDir, string OSPrefix, ref string PlatformSDKVersion, bool bVerbose, ILogger Logger)
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
									Major = Int32.Parse(Tokens[0]);
									Minor = Int32.Parse(Tokens[1]);
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
					Logger.LogInformation("Triggered an exception while looking for SDK directory in Xcode.app");
					Logger.LogInformation("{Ex}", Ex.ToString());
				}
			}

			// make sure we have a valid SDK directory
			if (!RuntimePlatform.IsWindows && !Directory.Exists(Path.Combine(BaseSDKDir, OSPrefix + PlatformSDKVersion + ".sdk")))
			{
				throw new BuildException("Invalid SDK {0}{1}.sdk, not found in {2}", OSPrefix, PlatformSDKVersion, BaseSDKDir);
			}

			if (bVerbose && !ProjectFileGenerator.bGenerateProjectFiles)
			{
				Logger.LogInformation("Compiling with {Os} SDK {Sdk}", OSPrefix, PlatformSDKVersion);
			}
		}
	}

	abstract class AppleToolChain : ClangToolChain
	{
		protected class AppleToolChainInfo : ClangToolChainInfo
		{
			public AppleToolChainInfo(FileReference Clang, FileReference Archiver, ILogger Logger)
				: base(Clang, Archiver, Logger)
			{
			}

			// libtool doesn't provide version, just use clang's version string
			/// <inheritdoc/>
			protected override string QueryArchiverVersionString() => ClangVersionString;
		}

		protected FileReference? ProjectFile;

		protected bool bUseModernXcode => AppleExports.UseModernXcode(ProjectFile);

		public AppleToolChain(FileReference? InProjectFile, ClangToolChainOptions InOptions, ILogger InLogger) : base(InOptions, InLogger)
		{
			ProjectFile = InProjectFile;
		}

		/// <summary>
		/// Takes an architecture string as provided by UBT for the target and formats it for Clang. Supports
		/// multiple architectures joined with '+'
		/// </summary>
		/// <param name="InArchitectures"></param>
		/// <returns></returns>
		protected string FormatArchitectureArg(UnrealArchitectures InArchitectures)
		{
			return "-arch " + String.Join('+', InArchitectures.Architectures.Select(x => x.AppleName));
		}

		protected DirectoryReference GetMacDevSrcRoot()
		{
			return Unreal.EngineSourceDirectory;
		}

		protected override CPPOutput CompileCPPFiles(CppCompileEnvironment CompileEnvironment, List<FileItem> InputFiles, DirectoryReference OutputDir, string ModuleName, IActionGraphBuilder Graph)
		{
			List<string> GlobalArguments = new();

			GetCompileArguments_Global(CompileEnvironment, GlobalArguments);

			List<FileItem> FrameworkTokenFiles = new List<FileItem>();
			foreach (UEBuildFramework Framework in CompileEnvironment.AdditionalFrameworks)
			{
				if (Framework.ZipFile != null)
				{
					// even in modern, we have to try to unzip in UBT, so that commandline builds extract the frameworks _before_ building (the Xcode finalize
					// happens after building). We still need the unzip in Xcode pre-build phase, because Xcode wants the frameworks around for its build graph
					ExtractFramework(Framework, Graph, Logger);
					FrameworkTokenFiles.Add(Framework.ExtractedTokenFile!);
				}
			}

			CPPOutput Result = new CPPOutput();
			// Create a compile action for each source file.
			foreach (FileItem SourceFile in InputFiles)
			{
				Action CompileAction = CompileCPPFile(CompileEnvironment, SourceFile, OutputDir, ModuleName, Graph, GlobalArguments, Result);
				CompileAction.PrerequisiteItems.UnionWith(FrameworkTokenFiles);
			}
			return Result;
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
			Utils.RunLocalProcessAndLogOutput(StartInfo, Logger);
		}

		/// <summary>
		/// Writes a versions.xcconfig file for xcode to pull in when making an app plist
		/// </summary>
		/// <param name="LinkEnvironment"></param>
		/// <param name="Prerequisite">FileItem describing the Prerequisite that this will this depends on (executable or similar) </param>
		/// <param name="Graph">List of actions to be executed. Additional actions will be added to this list.</param>
		protected FileItem UpdateVersionFile(LinkEnvironment LinkEnvironment, FileItem Prerequisite, IActionGraphBuilder Graph)
		{
			FileItem DestFile;

			// Make the compile action
			Action UpdateVersionAction = Graph.CreateAction(ActionType.CreateAppBundle);
			UpdateVersionAction.WorkingDirectory = GetMacDevSrcRoot();
			UpdateVersionAction.CommandPath = BuildHostPlatform.Current.Shell;
			UpdateVersionAction.CommandDescription = "";

			// @todo programs right nhow are sharing the Engine build version - one reason for this is that we can't get to the Engine/Programs directory from here
			// (we can't even get to the Engine/Source/Programs directory without searching on disk), and if we did, we would create a _lot_ of Engine/Programs directories
			// on disk that don't exist in p4. So, we just re-use Engine version, not Project version
			//				DirectoryReference ProductDirectory = FindProductDirectory(ProjectFile, LinkEnvironment.OutputDirectory!, Graph.Makefile.TargetType);
			DirectoryReference ProductDirectory = (ProjectFile?.Directory) ?? Unreal.EngineDirectory;
			FileReference OutputVersionFile = FileReference.Combine(ProductDirectory, "Intermediate/Build/Versions.xcconfig");
			DestFile = FileItem.GetItemByFileReference(OutputVersionFile);

			// make path to the script
			FileItem BundleScript = FileItem.GetItemByFileReference(FileReference.Combine(Unreal.EngineDirectory, "Build/BatchFiles/Mac/UpdateVersionAfterBuild.sh"));
			UpdateVersionAction.CommandArguments = $"\"{BundleScript.AbsolutePath}\" {ProductDirectory} {LinkEnvironment.Platform}";
			UpdateVersionAction.PrerequisiteItems.Add(Prerequisite);
			UpdateVersionAction.ProducedItems.Add(DestFile);
			UpdateVersionAction.StatusDescription = $"Updating version file: {OutputVersionFile}";

			return DestFile;
		}

		protected FileItem ExtractFramework(UEBuildFramework Framework, IActionGraphBuilder Graph, ILogger Logger)
		{
			if (Framework.ZipFile == null)
			{
				throw new BuildException("Unable to extract framework '{0}' - no zip file specified", Framework.Name);
			}
			if (!Framework.bHasMadeUnzipAction)
			{
				Framework.bHasMadeUnzipAction = true;

				FileItem InputFile = FileItem.GetItemByFileReference(Framework.ZipFile);

				StringBuilder ExtractScript = new StringBuilder();
				ExtractScript.AppendLine("#!/bin/sh");
				ExtractScript.AppendLine("set -e");
				// ExtractScript.AppendLine("set -x"); // For debugging
				ExtractScript.AppendLine(String.Format("[ -d {0} ] && rm -rf {0}", Utils.MakePathSafeToUseWithCommandLine(Framework.ZipOutputDirectory!.FullName)));
				ExtractScript.AppendLine(String.Format("unzip -q -o {0} -d {1}", Utils.MakePathSafeToUseWithCommandLine(Framework.ZipFile.FullName), Utils.MakePathSafeToUseWithCommandLine(Framework.ZipOutputDirectory.ParentDirectory!.FullName))); // Zip contains folder with the same name, hence ParentDirectory
				ExtractScript.AppendLine(String.Format("touch {0}", Utils.MakePathSafeToUseWithCommandLine(Framework.ExtractedTokenFile!.AbsolutePath)));

				FileItem ExtractScriptFileItem = Graph.CreateIntermediateTextFile(new FileReference(Framework.ZipOutputDirectory.FullName + ".sh"), ExtractScript.ToString());

				Action UnzipAction = Graph.CreateAction(ActionType.BuildProject);
				UnzipAction.CommandPath = new FileReference("/bin/sh");
				UnzipAction.CommandArguments = Utils.MakePathSafeToUseWithCommandLine(ExtractScriptFileItem.AbsolutePath);
				UnzipAction.WorkingDirectory = Unreal.EngineDirectory;
				UnzipAction.PrerequisiteItems.Add(InputFile);
				UnzipAction.PrerequisiteItems.Add(ExtractScriptFileItem);
				UnzipAction.ProducedItems.Add(Framework.ExtractedTokenFile);
				UnzipAction.DeleteItems.Add(Framework.ExtractedTokenFile);
				UnzipAction.StatusDescription = String.Format("Unzipping : {0} -> {1}", Framework.ZipFile, Framework.ZipOutputDirectory);
				UnzipAction.bCanExecuteRemotely = false;
			}
			return Framework.ExtractedTokenFile!;
		}

		/// <inheritdoc/>
		protected override string EscapePreprocessorDefinition(string Definition)
		{
			return Definition.Contains('"') ? Definition.Replace("\"", "\\\"") : Definition;
		}

		protected override void GetCppStandardCompileArgument(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			if (CompileEnvironment.bEnableObjCAutomaticReferenceCounting)
			{
				Arguments.Add("-fobjc-arc");
			}

			base.GetCppStandardCompileArgument(CompileEnvironment, Arguments);
		}

		protected override void GetCompileArguments_CPP(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.Add("-x objective-c++");
			GetCppStandardCompileArgument(CompileEnvironment, Arguments);
			Arguments.Add("-stdlib=libc++");
		}

		protected override void GetCompileArguments_MM(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.Add("-x objective-c++");
			GetCppStandardCompileArgument(CompileEnvironment, Arguments);
			Arguments.Add("-stdlib=libc++");
		}

		protected override void GetCompileArguments_M(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.Add("-x objective-c");
			GetCppStandardCompileArgument(CompileEnvironment, Arguments);
			Arguments.Add("-stdlib=libc++");
		}

		protected override void GetCompileArguments_PCH(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			Arguments.Add("-x objective-c++-header");
			GetCppStandardCompileArgument(CompileEnvironment, Arguments);
			Arguments.Add("-stdlib=libc++");
			Arguments.Add("-fpch-instantiate-templates");
		}

		/// <inheritdoc/>
		protected override void GetCompileArguments_WarningsAndErrors(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			base.GetCompileArguments_WarningsAndErrors(CompileEnvironment, Arguments);

			Arguments.Add("-Wno-unknown-warning-option");
			Arguments.Add("-Wno-range-loop-analysis");
			Arguments.Add("-Wno-single-bit-bitfield-constant-conversion");
		}

		/// <inheritdoc/>
		protected override void GetCompileArguments_Optimizations(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			base.GetCompileArguments_Optimizations(CompileEnvironment, Arguments);

			bool bStaticAnalysis = false;
			string? StaticAnalysisMode = Environment.GetEnvironmentVariable("CLANG_STATIC_ANALYZER_MODE");
			if (!String.IsNullOrEmpty(StaticAnalysisMode))
			{
				bStaticAnalysis = true;
			}

			// Optimize non- debug builds.
			if (CompileEnvironment.bOptimizeCode && !bStaticAnalysis)
			{
				// Don't over optimise if using AddressSanitizer or you'll get false positive errors due to erroneous optimisation of necessary AddressSanitizer instrumentation.
				if (Options.HasFlag(ClangToolChainOptions.EnableAddressSanitizer))
				{
					Arguments.Add("-O1");
					Arguments.Add("-g");
					Arguments.Add("-fno-optimize-sibling-calls");
					Arguments.Add("-fno-omit-frame-pointer");
				}
				else if (Options.HasFlag(ClangToolChainOptions.EnableThreadSanitizer))
				{
					Arguments.Add("-O1");
					Arguments.Add("-g");
				}
				else if (CompileEnvironment.OptimizationLevel == OptimizationMode.Size)
				{
					Arguments.Add("-Oz");
				}
				else if (CompileEnvironment.OptimizationLevel == OptimizationMode.SizeAndSpeed)
				{
					Arguments.Add("-Os");

					if (CompileEnvironment.Architecture == UnrealArch.Arm64)
					{
						Arguments.Add("-moutline");
					}
				}
				else
				{
					Arguments.Add("-O3");
				}
			}
			else
			{
				Arguments.Add("-O0");
			}
		}

		/// <inheritdoc/>
		protected override void GetCompileArguments_Debugging(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			base.GetCompileArguments_Debugging(CompileEnvironment, Arguments);

			// Create DWARF format debug info if wanted,
			if (CompileEnvironment.bCreateDebugInfo)
			{
				Arguments.Add("-gdwarf-4");
			}
		}

		/// <inheritdoc/>
		protected override void GetCompileArguments_Analyze(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			base.GetCompileArguments_Analyze(CompileEnvironment, Arguments);

			// Disable all clang tidy checks
			Arguments.Add($"-Xclang -analyzer-tidy-checker=-*");
		}

		/// <inheritdoc/>
		protected override void GetCompileArguments_Global(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			base.GetCompileArguments_Global(CompileEnvironment, Arguments);

			Arguments.Add(GetRTTIFlag(CompileEnvironment));

			Arguments.Add("-fmessage-length=0");
			Arguments.Add("-fpascal-strings");
		}

		public override CppCompileEnvironment CreateSharedResponseFile(CppCompileEnvironment CompileEnvironment, FileReference OutResponseFile, IActionGraphBuilder Graph)
		{
			// Temporarily turn of shared response files for apple toolchains
			return CompileEnvironment;
		}

		protected string GetDsymutilPath(ILogger Logger, out string ExtraOptions, bool bIsForLTOBuild = false)
		{
			FileReference DsymutilLocation = new FileReference("/usr/bin/dsymutil");

			// dsymutil before 10.0.1 has a bug that causes issues, it's fixed in autosdks but not everyone has those set up so for the timebeing we have
			// a version in P4 - first determine if we need it
			string DsymutilVersionString = Utils.RunLocalProcessAndReturnStdOut(DsymutilLocation.FullName, "-version", Logger);

			bool bUseInstalledDsymutil = true;
			int Major = 0, Minor = 0, Patch = 0;

			// tease out the version number
			string[] Tokens = DsymutilVersionString.Split(" ".ToCharArray());

			// sanity check
			if (Tokens.Length < 4 || Tokens[3].Contains('.') == false)
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
					if (!Int32.TryParse(Versions[0], out Major) || !Int32.TryParse(Versions[1], out Minor) || !Int32.TryParse(Versions[2], out Patch))
					{
						Log.TraceInformationOnce("Unable to parse version tokens: {0}", Tokens[3]);
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

		public override ICollection<FileItem> PostBuild(ReadOnlyTargetRules Target, FileItem Executable, LinkEnvironment BinaryLinkEnvironment, IActionGraphBuilder Graph)
		{
			List<FileItem> OutputFiles = new List<FileItem>(base.PostBuild(Target, Executable, BinaryLinkEnvironment, Graph));

			bool bIsBuildingAppBundle = !BinaryLinkEnvironment.bIsBuildingDLL && !BinaryLinkEnvironment.bIsBuildingLibrary && !BinaryLinkEnvironment.bIsBuildingConsoleApplication;
			if (AppleExports.UseModernXcode(Target.ProjectFile) && bIsBuildingAppBundle)
			{
				Action PostBuildAction = ApplePostBuildSyncMode.CreatePostBuildSyncAction(Target, Executable, BinaryLinkEnvironment.IntermediateDirectory!, Graph);

				PostBuildAction.PrerequisiteItems.UnionWith(OutputFiles);
				OutputFiles.AddRange(PostBuildAction.ProducedItems);
			}

			return OutputFiles;
		}

		#region Stub Xcode Projects

		internal static bool GenerateProjectFiles(FileReference? ProjectFile, string[] Arguments, ILogger Logger, out DirectoryReference? XcodeProjectFile)
		{
			ProjectFileGenerator.bGenerateProjectFiles = true;
			try
			{
				CommandLineArguments CmdLine = new CommandLineArguments(Arguments);

				PlatformProjectGeneratorCollection PlatformProjectGenerators = new PlatformProjectGeneratorCollection();
				PlatformProjectGenerators.RegisterPlatformProjectGenerator(UnrealTargetPlatform.Mac, new MacProjectGenerator(CmdLine, Logger), Logger);
				PlatformProjectGenerators.RegisterPlatformProjectGenerator(UnrealTargetPlatform.IOS, new IOSProjectGenerator(CmdLine, Logger), Logger);
				PlatformProjectGenerators.RegisterPlatformProjectGenerator(UnrealTargetPlatform.TVOS, new TVOSProjectGenerator(CmdLine, Logger), Logger);

				XcodeProjectFileGenerator Generator = new XcodeProjectFileGenerator(ProjectFile, CmdLine);
				// don't need the editor data since these are stub projects
				bool bSucces = Generator.GenerateProjectFiles(PlatformProjectGenerators, Arguments, bCacheDataForEditor: false, Logger);
				XcodeProjectFile = Generator.XCWorkspace;
				return bSucces;
			}
			catch (Exception ex)
			{
				XcodeProjectFile = null;
				Logger.LogError(ex.ToString());
			}
			finally
			{
				ProjectFileGenerator.bGenerateProjectFiles = false;
			}
			return false;
		}

		/// <summary>
		/// Genearate an run-only Xcode project, that is not meant to be used for anything else besides code-signing/running/etc of the native .app bundle
		/// </summary>
		/// <param name="UProjectFile">Location of .uproject file (or null for the engine project</param>
		/// <param name="Platform">The platform to generate a project for</param>
		/// <param name="TargetName">The name of the target being built, so we can generate a more minimal project</param>
		/// <param name="bForDistribution">True if this is making a bild for uploading to app store</param>
		/// <param name="Logger">Logging object</param>
		/// <param name="GeneratedProjectFile">Returns the .xcworkspace that was made</param>
		internal static void GenerateRunOnlyXcodeProject(FileReference? UProjectFile, UnrealTargetPlatform Platform, string TargetName, bool bForDistribution, ILogger Logger, out DirectoryReference? GeneratedProjectFile)
		{
			List<string> Options = new()
			{
				$"-platforms={Platform}",
				"-DeployOnly",
				"-NoIntellisense",
				"-NoDotNet",
				"-IgnoreJunk",
				bForDistribution ? "-distribution" : "-development",
				"-IncludeTempTargets",
				"-projectfileformat=XCode",
				"-automated",
			};

			if (!String.IsNullOrEmpty(TargetName))
			{
				Options.Add($"-singletarget={TargetName}");
			}

			if (UProjectFile == null || UProjectFile.IsUnderDirectory(Unreal.EngineDirectory))
			{
				// @todo do we need these? where would the bundleid come from if there's no project?
				//				Options.Add("-bundleID=" + BundleID);
				//				Options.Add("-appname=" + AppName);
				// @todo add an option to only add Engine target?
			}
			else
			{
				Options.Add($"-project=\"{UProjectFile.FullName}\"");
				Options.Add("-game");
			}

			// we need to be in Engine/Source for some build.cs files
			string CurrentCWD = Environment.CurrentDirectory;
			Environment.CurrentDirectory = DirectoryReference.Combine(Unreal.EngineDirectory, "Source").FullName;
			GenerateProjectFiles(UProjectFile, Options.ToArray(), Logger, out GeneratedProjectFile);
			Environment.CurrentDirectory = CurrentCWD;
		}

		internal static int FinalizeAppWithXcode(DirectoryReference XcodeProject, UnrealTargetPlatform Platform, string SchemeName, string Configuration, string Action, string ExtraOptions, ILogger Logger)
		{
			List<string> Arguments = new()
			{
				"UBT_NO_POST_DEPLOY=true",
				new IOSToolChainSettings(Logger).XcodeDeveloperDir + "usr/bin/xcodebuild",
				Action,
				$"-workspace \"{XcodeProject.FullName}\"",
				$"-scheme \"{SchemeName}\"",
				$"-configuration \"{Configuration}\"",
				$"-destination generic/platform=" + (Platform == UnrealTargetPlatform.TVOS ? "tvOS" : Platform == UnrealTargetPlatform.Mac ? "macOS" : "iOS"),
				"-allowProvisioningUpdates",
				"-hideShellScriptEnvironment",
				// xcode gets confused it we _just_ wrote out entitlements while generating the temp project, and it thinks it was modified _during_ building
				// but it wasn't, it was written before the build started
				"CODE_SIGN_ALLOW_ENTITLEMENTS_MODIFICATION=YES",
				ExtraOptions,
				//$"-sdk {SDKName}",
			};

			return Utils.RunLocalProcessAndLogOutput("/usr/bin/env", String.Join(" ", Arguments), Logger);
		}

		#endregion
	};

	[Serializable]
	class ApplePostBuildSyncTarget
	{
		public FileReference? ProjectFile;
		public UnrealTargetPlatform Platform;
		public UnrealTargetConfiguration Configuration;
		public string TargetName;

		public ApplePostBuildSyncTarget(ReadOnlyTargetRules Target)
		{
			Platform = Target.Platform;
			Configuration = Target.Configuration;
			ProjectFile = Target.ProjectFile;
			TargetName = Target.Name;
		}
	}

	[ToolMode("ApplePostBuildSync", ToolModeOptions.XmlConfig | ToolModeOptions.BuildPlatforms)]
	class ApplePostBuildSyncMode : ToolMode
	{
		[CommandLine("-Input=", Required = true)]
		public FileReference? InputFile = null;

		[CommandLine("-XmlConfigCache=")]
		public FileReference? XmlConfigCache = null;

		// this isn't actually used, but is helpful to pass -modernxcode along in CreatePostBuildSyncAction, and UBT won't
		// complain that nothing is using it, because where we _do_ use it is outside the normal cmdline parsing functionality
		[CommandLine("-ModernXcode")]
		public bool bModernXcode;

		public override Task<int> ExecuteAsync(CommandLineArguments Arguments, ILogger Logger)
		{
			Arguments.ApplyTo(this);
			Arguments.CheckAllArgumentsUsed();

			// Run the PostBuildSync command
			ApplePostBuildSyncTarget Target = BinaryFormatterUtils.Load<ApplePostBuildSyncTarget>(InputFile!);
			int ExitCode = PostBuildSync(Target, Logger);

			return Task.FromResult(ExitCode);
		}

		private int PostBuildSync(ApplePostBuildSyncTarget Target, ILogger Logger)
		{
			// if xcode is building this, it will also do the Run stuff anyway, so no need to do it here as well
			if (Environment.GetEnvironmentVariable("UE_BUILD_FROM_XCODE") == "1")
			{
				return 0;
			}

			int ExitCode = AppleExports.BuildWithStubXcodeProject(Target.ProjectFile, Target.Platform, Target.Configuration, Target.TargetName, AppleExports.XcodeBuildMode.PostBuildSync, Logger);
			if (ExitCode != 0)
			{
				Logger.LogError("ERROR: Failed to finalize the .app with Xcode. Check the log for ,more information");
			}

			return ExitCode;
		}

		private static FileItem GetPostBuildOutputFile(FileReference Executable, string TargetName, UnrealTargetPlatform Platform)
		{
			FileReference StagedExe;
			if (Platform == UnrealTargetPlatform.Mac)
			{
				StagedExe = FileReference.Combine(Executable.Directory, Executable.GetFileName() + ".app/Contents/PkgInfo");
			}
			else
			{
				StagedExe = FileReference.Combine(Executable.Directory, TargetName + ".app", TargetName);
			}
			return FileItem.GetItemByFileReference(StagedExe);
		}

		public static Action CreatePostBuildSyncAction(ReadOnlyTargetRules Target, FileItem Executable, DirectoryReference IntermediateDir, IActionGraphBuilder Graph)
		{
			ApplePostBuildSyncTarget PostBuildSync = new(Target);
			FileReference PostBuildSyncFile = FileReference.Combine(IntermediateDir!, "PostBuildSync.dat");
			BinaryFormatterUtils.Save(PostBuildSyncFile, PostBuildSync);

			string PostBuildSyncArguments = String.Format("-modernxcode -Input=\"{0}\" -XmlConfigCache=\"{1}\" -remoteini=\"{2}\"", PostBuildSyncFile, XmlConfig.CacheFile, UnrealBuildTool.GetRemoteIniPath());
			Action PostBuildSyncAction = Graph.CreateRecursiveAction<ApplePostBuildSyncMode>(ActionType.CreateAppBundle, PostBuildSyncArguments);

			PostBuildSyncAction.WorkingDirectory = Unreal.EngineSourceDirectory;
			PostBuildSyncAction.PrerequisiteItems.Add(Executable);
			PostBuildSyncAction.ProducedItems.Add(GetPostBuildOutputFile(Executable.Location, Target.Name, Target.Platform));
			PostBuildSyncAction.StatusDescription = $"Executing PostBuildSync [{Executable.Location}]";
			PostBuildSyncAction.bCanExecuteRemotely = false;

			return PostBuildSyncAction;
		}
	}
}
