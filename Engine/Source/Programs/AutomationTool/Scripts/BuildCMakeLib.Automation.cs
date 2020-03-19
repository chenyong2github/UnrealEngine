// Copyright Epic Games, Inc. All Rights Reserved.
using AutomationTool;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using Tools.DotNETCommon;
using UnrealBuildTool;

// TODO Currently this only supports one lib and one platform at a time.
// The reason for this is that the the library version and additional arguments (which are per platform) is passed on the command line.
// This could be improved by loading the configuration (per lib, and per lib+platform) for each lib, either from a .cs or an .ini and aligning those values with the Build.cs.
// Additionally this is adapted from the BuildPhysX automation script, so some of this could be aligned there as well.
[Help("Builds a third party library using the CMake build system.")]
[Help("TargetLib", "Specify the target library to build.")]
[Help("TargetLibVersion", "Specify the target library version to build.")]
[Help("TargetLibSourcePath", "Override the path to source, if external to the engine. (eg. -TargetLibSourcePath=path). Default is empty.")]
[Help("TargetPlatform", "Specify the name of the target platform to build (eg. -TargetPlatform=IOS).")]
[Help("TargetConfigs", "Specify a list of configurations to build, separated by '+' characters (eg. -TargetConfigs=release+debug). Default is release+debug.")]
[Help("LibOutputPath", "Override the path to output the libs to. (eg. -LibOutputPath=lib). Default is empty.")]
[Help("CMakeGenerator", "Specify the CMake generator to use.")]
[Help("CMakeProjectIncludeFile", "Specify the name of the CMake project include file to use, first looks in current directory then looks in global directory.")]
[Help("CMakeAdditionalArguments", "Specify the additional commandline to pass to cmake.")]
[Help("MakeTarget", "Override the target to pass to make.")]
[Help("SkipCreateChangelist", "Do not create a P4 changelist for source or libs. If this argument is not supplied source and libs will be added to a Perforce changelist.")]
[Help("SkipSubmit", "Do not perform P4 submit of source or libs. If this argument is not supplied source and libs will be automatically submitted to Perforce. If SkipCreateChangelist is specified, this argument applies by default.")]
[Help("Robomerge", "Which robomerge action to apply to the submission. If we're skipping submit, this is not used.")]
[RequireP4]
public sealed class BuildCMakeLib : BuildCommand
{
	public class TargetLib
	{
		public string Name = "";
		public string Version = "";
		public string SourcePath = "";
		public string LibOutputPath = "";
		public string CMakeProjectIncludeFile = "";
		public string CMakeAdditionalArguments = "";
		public string MakeTarget = "";

		public virtual Dictionary<string, string> BuildMap => new Dictionary<string, string>()
		{
			{ "debug",   "debug"   },
			{ "release", "release" }
		};

		public virtual Dictionary<string, string> BuildSuffix => new Dictionary<string, string>()
		{
			{ "debug",   "" },
			{ "release", "" }
		};

		public static DirectoryReference ThirdPartySourceDirectory = DirectoryReference.Combine(RootDirectory, "Engine", "Source", "ThirdParty");

		public DirectoryReference GetLibSourceDirectory() 
		{ 
			if (string.IsNullOrEmpty(SourcePath))
			{ 
				return DirectoryReference.Combine(ThirdPartySourceDirectory, Name, Version); 
			}
			else 
			{
				return new DirectoryReference(SourcePath);
			} 
		}

		public override string ToString() => Name;
	}

	public static void MakeFreshDirectoryIfRequired(DirectoryReference Directory)
	{
		if (!DirectoryReference.Exists(Directory))
		{
			DirectoryReference.CreateDirectory(Directory);
		}
		else
		{
			InternalUtils.SafeDeleteDirectory(Directory.FullName);
			DirectoryReference.CreateDirectory(Directory);
		}
	}

	public abstract class TargetPlatform : CommandUtils
	{
		public static DirectoryReference CMakeRootDirectory = DirectoryReference.Combine(RootDirectory, "Engine", "Extras", "ThirdPartyNotUE", "CMake");
		public static DirectoryReference MakeRootDirectory = DirectoryReference.Combine(RootDirectory, "Engine", "Extras", "ThirdPartyNotUE", "GNU_Make", "make-3.81");

		private DirectoryReference PlatformEngineRoot => IsPlatformExtension
			? DirectoryReference.Combine(RootDirectory, "Engine", "Platforms", Platform.ToString())
			: DirectoryReference.Combine(RootDirectory, "Engine");

		private DirectoryReference GetTargetLibRootDirectory(TargetLib TargetLib)
		{
			return DirectoryReference.Combine(PlatformEngineRoot, "Source", "ThirdParty", TargetLib.Name, TargetLib.Version);
		}

		private DirectoryReference GetTargetLibBaseRootDirectory(TargetLib TargetLib)
		{
			return DirectoryReference.Combine(RootDirectory, "Engine", "Source", "ThirdParty", TargetLib.Name, TargetLib.Version);
		}

		private DirectoryReference GetTargetLibBuildScriptDirectory(TargetLib TargetLib)
		{
			// Some libraries use BuildForUE4 instead of BuildForUE, check this here
			DirectoryReference BuildForUEDirectory = DirectoryReference.Combine(GetTargetLibRootDirectory(TargetLib), "BuildForUE");
			if (!DirectoryReference.Exists(BuildForUEDirectory))
			{
				// If not available then check BuildForUE4
				BuildForUEDirectory = DirectoryReference.Combine(GetTargetLibRootDirectory(TargetLib), "BuildForUE4");
			}

			return BuildForUEDirectory;
		}

		private DirectoryReference GetTargetLibBaseBuildScriptDirectory(TargetLib TargetLib)
		{
			// Some libraries use BuildForUE4 instead of BuildForUE, check this here
			DirectoryReference BuildForUEDirectory = DirectoryReference.Combine(GetTargetLibBaseRootDirectory(TargetLib), "BuildForUE");
			if (!DirectoryReference.Exists(BuildForUEDirectory))
			{
				// If not available then check BuildForUE4
				BuildForUEDirectory = DirectoryReference.Combine(GetTargetLibBaseRootDirectory(TargetLib), "BuildForUE4");
			}

			return BuildForUEDirectory;
		}

		protected DirectoryReference GetTargetLibPlatformCMakeDirectory(TargetLib TargetLib) 
		{
			// Possible "standard" locations for the CMakesLists.txt are BuildForUE/Platform, BuildForUE or the source root

			// First check for an overriden CMakeLists.txt in the BuildForUE/Platform directory
			DirectoryReference CMakeDirectory = GetTargetLibBuildScriptDirectory(TargetLib);
			if (!FileReference.Exists(FileReference.Combine(CMakeDirectory, IsPlatformExtension ? "" : Platform.ToString(), "CMakeLists.txt")))
			{
				// If not available then check BuildForUE
				CMakeDirectory = GetTargetLibBaseBuildScriptDirectory(TargetLib);
				if (!FileReference.Exists(FileReference.Combine(CMakeDirectory, "CMakeLists.txt")))
				{
					// If not available then check the lib source root
					CMakeDirectory = TargetLib.GetLibSourceDirectory();
				}
			}

			return CMakeDirectory;
		}

		protected DirectoryReference GetProjectsDirectory(TargetLib TargetLib, string TargetConfiguration) =>
			DirectoryReference.Combine(GetTargetLibRootDirectory(TargetLib), "Build",
				IsPlatformExtension ? "" : TargetBuildPlatform,
				PlatformBuildSubdirectory ?? "",
				SeparateProjectPerConfig ? TargetLib.BuildMap[TargetConfiguration] : "");

		protected FileReference GetToolchainPath(TargetLib TargetLib, string TargetConfiguration)
		{
			string ToolchainName = GetToolchainName(TargetLib, TargetConfiguration);

			if (ToolchainName == null)
			{
				return null;
			}

			// First check for an overriden toolchain in the BuildForUE/Platform directory
			FileReference ToolChainPath = FileReference.Combine(GetTargetLibBuildScriptDirectory(TargetLib), IsPlatformExtension ? "" : Platform.ToString(), ToolchainName);
			if (!FileReference.Exists(ToolChainPath))
			{
				// If not available then use the top level toolchain path
				ToolChainPath = FileReference.Combine(PlatformEngineRoot, "Source", "ThirdParty", "CMake", "PlatformScripts", IsPlatformExtension ? "" : Platform.ToString(), ToolchainName);
			}

			return ToolChainPath;
		}

		protected FileReference GetProjectIncludePath(TargetLib TargetLib, string TargetConfiguration)
		{
			return FileReference.Combine(GetTargetLibBuildScriptDirectory(TargetLib), IsPlatformExtension ? "" : Platform.ToString(), TargetLib.CMakeProjectIncludeFile);
		}

		protected DirectoryReference GetOutputLibraryDirectory(TargetLib TargetLib, string TargetConfiguration)
		{
			return DirectoryReference.Combine(GetTargetLibRootDirectory(TargetLib), TargetLib.LibOutputPath, IsPlatformExtension ? "" : Platform.ToString(), PlatformBuildSubdirectory ?? "", TargetConfiguration);
		}

		protected DirectoryReference GetOutputBinaryDirectory(TargetLib TargetLib, string TargetConfiguration)
		{
			return DirectoryReference.Combine(PlatformEngineRoot, IsPlatformExtension ? "" : Platform.ToString(), PlatformBuildSubdirectory ?? "", TargetConfiguration);
		}

		public abstract UnrealTargetPlatform Platform { get; }

		public abstract bool HasBinaries { get; }

		public abstract string DebugDatabaseExtension { get; }
		public abstract string DynamicLibraryExtension { get; }
		public abstract string StaticLibraryExtension { get; }
		public abstract bool IsPlatformExtension { get; }
		public abstract bool UseResponseFiles { get; }
		public abstract string TargetBuildPlatform { get; }
		public abstract bool SeparateProjectPerConfig { get; }
		public abstract string CMakeGeneratorName { get; }

		public virtual string PlatformBuildSubdirectory => null;
		public virtual string FriendlyName => Platform.ToString();

		public virtual string CMakeCommand => BuildHostPlatform.Current.Platform.IsInGroup(UnrealPlatformGroup.Windows)
			? FileReference.Combine(CMakeRootDirectory, "bin", "cmake.exe").FullName
			: FileReference.Combine(CMakeRootDirectory, "bin", "cmake").FullName;

		public virtual string MakeCommand => BuildHostPlatform.Current.Platform.IsInGroup(UnrealPlatformGroup.Windows)
			? FileReference.Combine(MakeRootDirectory, "bin", "make.exe").FullName
			: FileReference.Combine(MakeRootDirectory, "bin", "make").FullName;

		public abstract bool SupportsTargetLib(TargetLib Library);

		public virtual string GetToolchainName(TargetLib TargetLib, string TargetConfiguration) => FriendlyName + ".cmake";

		public virtual string GetAdditionalCMakeArguments(TargetLib TargetLib, string TargetConfiguration) => " " + TargetLib.CMakeAdditionalArguments;

		public virtual string GetCMakeArguments(TargetLib TargetLib, string TargetConfiguration)
		{
			string Args = "\"" + GetTargetLibPlatformCMakeDirectory(TargetLib).FullName + "\"";
			Args += " -G \"" + CMakeGeneratorName + "\"";

			if (SeparateProjectPerConfig)
			{
				Args += " -DCMAKE_BUILD_TYPE=\"" + TargetConfiguration + "\"";
			}

			FileReference ToolchainPath = GetToolchainPath(TargetLib, TargetConfiguration);
			if (ToolchainPath != null && FileReference.Exists(ToolchainPath))
			{
				Args += " -DCMAKE_TOOLCHAIN_FILE=\"" + ToolchainPath.FullName + "\"";
			}

			FileReference ProjectIncludePath = GetProjectIncludePath(TargetLib, TargetConfiguration);
			if (ProjectIncludePath != null && FileReference.Exists(ProjectIncludePath))
			{
				Args += " -DCMAKE_PROJECT_INCLUDE_FILE=\"" + ProjectIncludePath.FullName + "\"";
			}
 
			Args += " -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=\"" + GetOutputLibraryDirectory(TargetLib, TargetConfiguration) + "\"";

			if (HasBinaries)
			{
				Args += " -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=\"" + GetOutputBinaryDirectory(TargetLib, TargetConfiguration) + "\"";
				Args += " -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=\"" + GetOutputBinaryDirectory(TargetLib, TargetConfiguration) + "\"";
			}

			if (UseResponseFiles)
			{
				// Enable response files for platforms that require them.
				// Response files are used for include paths etc, to fix max command line length issues.
				Args += " -DUSE_RESPONSE_FILES=1";
			}

			string AdditionalArgs = GetAdditionalCMakeArguments(TargetLib, TargetConfiguration);
			if (AdditionalArgs != null)
			{
				Args += AdditionalArgs.Replace("${TARGET_CONFIG}", TargetConfiguration ?? "");
			}

			return Args;
		}

		public virtual IEnumerable<FileReference> EnumerateOutputFiles(DirectoryReference BaseDir, string SearchPrefix, TargetLib TargetLib)
		{
			if (!DirectoryReference.Exists(BaseDir))
			{ 
				yield break;
			}

			foreach (FileReference File in DirectoryReference.EnumerateFiles(BaseDir, SearchPrefix))
			{
				var FileNameUpper = File.GetFileName().ToUpper();
				if (FileNameUpper.Contains(TargetLib.Name.ToUpper()))
				{
					yield return File;
				}
			}
		}

		public virtual IEnumerable<FileReference> EnumerateOutputFiles(TargetLib TargetLib, string TargetConfiguration)
		{
			string SearchPrefix = "*" + TargetLib.BuildSuffix[TargetConfiguration] + ".";

			DirectoryReference OutputLibraryDirectory = GetOutputLibraryDirectory(TargetLib, TargetConfiguration);

			// Scan static libraries directory
			IEnumerable<FileReference> Results = EnumerateOutputFiles(OutputLibraryDirectory, SearchPrefix + StaticLibraryExtension, TargetLib);
			if (DebugDatabaseExtension != null)
			{
				Results = Results.Concat(EnumerateOutputFiles(OutputLibraryDirectory, SearchPrefix + DebugDatabaseExtension, TargetLib));
			}

			// Scan dynamic libraries directory
			if (HasBinaries)
			{
				DirectoryReference OutputBinaryDirectory = GetOutputBinaryDirectory(TargetLib, TargetConfiguration);

				Results = Results.Concat(EnumerateOutputFiles(OutputBinaryDirectory, SearchPrefix + DynamicLibraryExtension, TargetLib));
				if (DebugDatabaseExtension != null)
				{
					Results = Results.Concat(EnumerateOutputFiles(OutputBinaryDirectory, SearchPrefix + DebugDatabaseExtension, TargetLib));
				}
			}

			return Results;
		}

		public virtual void SetupTargetLib(TargetLib TargetLib, string TargetConfiguration)
		{
			LogInformation("Building {0} for {1} ({2})...", TargetLib.Name, TargetBuildPlatform, TargetConfiguration);

			if (BuildHostPlatform.Current.Platform.IsInGroup(UnrealPlatformGroup.Unix))
			{
				Environment.SetEnvironmentVariable("CMAKE_ROOT", DirectoryReference.Combine(CMakeRootDirectory, "share").FullName);
				LogInformation("set {0}={1}", "CMAKE_ROOT", Environment.GetEnvironmentVariable("CMAKE_ROOT"));
			}

			DirectoryReference CMakeTargetDirectory = GetProjectsDirectory(TargetLib, TargetConfiguration);
			MakeFreshDirectoryIfRequired(CMakeTargetDirectory);

			LogInformation("Generating projects for lib " + TargetLib.Name + ", " + FriendlyName);

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.FileName = CMakeCommand;
			StartInfo.WorkingDirectory = CMakeTargetDirectory.FullName;
			StartInfo.Arguments = GetCMakeArguments(TargetLib, TargetConfiguration);

			if (Utils.RunLocalProcessAndLogOutput(StartInfo) != 0)
			{
				throw new AutomationException("Unable to generate projects for {0}.", TargetLib.ToString() + ", " + FriendlyName);
			}
		}

		public abstract void BuildTargetLib(TargetLib TargetLib, string TargetConfiguration);

		public virtual void CleanupTargetLib(TargetLib TargetLib, string TargetConfiguration)
		{
			if (string.IsNullOrEmpty(TargetConfiguration))
			{
				InternalUtils.SafeDeleteDirectory(DirectoryReference.Combine(GetTargetLibRootDirectory(TargetLib), "Build").FullName);
			}
			else
			{	
				DirectoryReference CMakeTargetDirectory = GetProjectsDirectory(TargetLib, TargetConfiguration);
				InternalUtils.SafeDeleteDirectory(CMakeTargetDirectory.FullName);
			}
		}
	}

	public abstract class NMakeTargetPlatform : TargetPlatform
	{
		public override bool SeparateProjectPerConfig => true;

		public override string CMakeGeneratorName => "NMake Makefiles";

		public override void BuildTargetLib(TargetLib TargetLib, string TargetConfiguration)
		{
			DirectoryReference ConfigDirectory = GetProjectsDirectory(TargetLib, TargetConfiguration);

			string Makefile = FileReference.Combine(ConfigDirectory, "Makefile").FullName;
			if (!FileExists(Makefile))
			{
				throw new AutomationException("Unabled to build {0} - file not found.", Makefile);
			}

			DirectoryReference CommonToolsPath = new DirectoryReference(System.Environment.GetEnvironmentVariable("VS140COMNTOOLS"));

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.FileName = "cmd.exe";
			StartInfo.WorkingDirectory = ConfigDirectory.FullName;

			StartInfo.Arguments = string.Format("/C \"{0}\" amd64 && nmake {1}", FileReference.Combine(CommonToolsPath, "..", "..", "VC", "vcvarsall.bat").FullName, TargetLib.MakeTarget);

			LogInformation("Working in: {0}", StartInfo.WorkingDirectory);
			LogInformation("{0} {1}", StartInfo.FileName, StartInfo.Arguments);

			if (Utils.RunLocalProcessAndLogOutput(StartInfo) != 0)
			{
				throw new AutomationException("Unabled to build {0}. Build process failed.", Makefile);
			}
		}
	}

	public abstract class MakefileTargetPlatform : TargetPlatform
	{
		public virtual string MakeOptions => "-j " + Environment.ProcessorCount;

		public override bool SeparateProjectPerConfig => true;

		public override string CMakeGeneratorName => "Unix Makefiles";

		public override void BuildTargetLib(TargetLib TargetLib, string TargetConfiguration)
		{
			DirectoryReference ConfigDirectory = GetProjectsDirectory(TargetLib, TargetConfiguration);
			Environment.SetEnvironmentVariable("LIB_SUFFIX", TargetLib.BuildSuffix[TargetConfiguration]);

			string Makefile = FileReference.Combine(ConfigDirectory, "Makefile").FullName;
			if (!FileExists(Makefile))
			{
				throw new AutomationException("Unabled to build {0} - file not found.", Makefile);
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.FileName = MakeCommand;
			StartInfo.WorkingDirectory = ConfigDirectory.FullName;

			// Bundled GNU make does not pass job number to subprocesses on Windows, work around that...
			// Redefining the MAKE variable will cause the -j flag to be passed to child make instances.
			StartInfo.Arguments = BuildHostPlatform.Current.Platform.IsInGroup(UnrealPlatformGroup.Windows)
				? string.Format("{1} {2} \"MAKE={0} {1}\"", MakeCommand, MakeOptions, TargetLib.MakeTarget)
				: string.Format("{0} {1}", MakeOptions, TargetLib.MakeTarget);

			LogInformation("Working in: {0}", StartInfo.WorkingDirectory);
			LogInformation("{0} {1}", StartInfo.FileName, StartInfo.Arguments);

			if (Utils.RunLocalProcessAndLogOutput(StartInfo) != 0)
			{
				throw new AutomationException("Unabled to build {0}. Build process failed.", Makefile);
			}
		}
	}

	public abstract class XcodeTargetPlatform : TargetPlatform
	{
		public override string CMakeGeneratorName => "Xcode";

		public override bool SeparateProjectPerConfig => false;

		public override void BuildTargetLib(TargetLib TargetLib, string TargetConfiguration)
		{
			DirectoryReference Directory = GetProjectsDirectory(TargetLib, TargetConfiguration);

			string ProjectFile = FileReference.Combine(Directory, TargetLib.ToString() + ".xcodeproj").FullName;
			if (!DirectoryExists(ProjectFile))
			{
				throw new AutomationException("Unabled to build project {0}. Project file not found.", ProjectFile);
			}
			
			RunAndLog(CmdEnv, "/usr/bin/xcodebuild", string.Format("-project \"{0}\" -target=\"ALL_BUILD\" -configuration {1} -quiet", ProjectFile, TargetConfiguration));
		}
	}

	private TargetLib GetTargetLib()
	{
		TargetLib TargetLib = new TargetLib();

		TargetLib.Name = ParseParamValue("TargetLib", "");
		TargetLib.Version = ParseParamValue("TargetLibVersion", "");
		TargetLib.SourcePath = ParseParamValue("TargetLibSourcePath", "");
		TargetLib.LibOutputPath = ParseParamValue("LibOutputPath", "");
		TargetLib.CMakeProjectIncludeFile = ParseParamValue("CMakeProjectIncludeFile", "");
		TargetLib.CMakeAdditionalArguments = ParseParamValue("CMakeAdditionalArguments", "");
		TargetLib.MakeTarget = ParseParamValue("MakeTarget", TargetLib.Name).ToLower();

		if (string.IsNullOrEmpty(TargetLib.Name) || string.IsNullOrEmpty(TargetLib.Version))
		{
			throw new AutomationException("Must specify both -TargetLib and -TargetLibVersion");
		}

		return TargetLib;
	}

	public List<string> GetTargetConfigurations()
	{
		List<string> TargetConfigs = new List<string>();

		string TargetConfigFilter = ParseParamValue("TargetConfigs", "debug+release");
		if (TargetConfigFilter != null)
		{
			foreach (string TargetConfig in TargetConfigFilter.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries))
			{
				TargetConfigs.Add(TargetConfig);
			}
		}

		return TargetConfigs;
	}

	private TargetPlatform GetTargetPlatform()
	{
		// Grab all the non-abstract subclasses of TargetPlatform from the executing assembly.
		var AvailablePlatformTypes = from Assembly in AppDomain.CurrentDomain.GetAssemblies()
									 from Type in Assembly.GetTypes()
									 where !Type.IsAbstract && Type.IsSubclassOf(typeof(TargetPlatform))
									 select Type;

		var PlatformTypeMap = new Dictionary<string, Type>();

		foreach (var Type in AvailablePlatformTypes)
		{
			int Index = Type.Name.IndexOf('_');
			if (Index == -1)
			{
				throw new BuildException("Invalid BuildCMakeLib target platform type found: {0}", Type);
			}
			
			PlatformTypeMap.Add(Type.Name, Type);
		}

		TargetPlatform TargetPlatform = null;

		// TODO For now the CMakeGenerateor and TargetPlatform are combined.
		string TargetPlatformName = ParseParamValue("TargetPlatform", null);
		string CMakeGenerator = ParseParamValue("CMakeGenerator", null);
		if (TargetPlatformName != null && CMakeGenerator != null)
		{
			var SelectedPlatform = CMakeGenerator + "TargetPlatform_" + TargetPlatformName;

			if (!PlatformTypeMap.ContainsKey(SelectedPlatform))
			{
				throw new BuildException("Unknown BuildCMakeLib target platform specified: {0}", SelectedPlatform);
			}

			var SelectedType = PlatformTypeMap[SelectedPlatform];
			var Constructors = SelectedType.GetConstructors();
			if (Constructors.Length != 1)
			{
				throw new BuildException("BuildCMakeLib build platform implementation type \"{0}\" should have exactly one constructor.", SelectedType);
			}

			var Parameters = Constructors[0].GetParameters();
			if (Parameters.Length >= 2)
			{
				throw new BuildException("The constructor for the target platform type \"{0}\" must take exactly zero or one arguments.", TargetPlatformName);
			}

			if (Parameters.Length == 1 && Parameters[0].ParameterType != typeof(string))
			{
				throw new BuildException("The constructor for the target platform type \"{0}\" has an invalid argument type. The type must be a string.", TargetPlatformName);
			}

			TargetPlatform = (TargetPlatform)Activator.CreateInstance(SelectedType, null);
		}

		return TargetPlatform;
	}

	private static string RemoveOtherMakeAndCygwinFromPath(string WindowsPath)
	{
		string[] PathComponents = WindowsPath.Split(';');
		string NewPath = "";
		foreach(string PathComponent in PathComponents)
		{
			// Everything that contains /bin or /sbin is suspicious, check if it has make in it
			if (PathComponent.Contains("\\bin") || PathComponent.Contains("/bin") || PathComponent.Contains("\\sbin") || PathComponent.Contains("/sbin"))
			{
				if (File.Exists(PathComponent + "/make.exe") || File.Exists(PathComponent + "make.exe") || File.Exists(PathComponent + "/cygwin1.dll"))
				{
					LogInformation("Removing {0} from PATH since it contains possibly colliding make.exe", PathComponent);
					continue;
				}
			}

			NewPath = NewPath + ';' + PathComponent + ';';
		}

		return NewPath;
	}

	private void SetupBuildEnvironment()
	{
		if (!Utils.IsRunningOnMono)
		{
			// ================================================================================
			// ThirdPartyNotUE
			// NOTE: these are Windows executables
			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			{
				string CMakePath = DirectoryReference.Combine(TargetPlatform.CMakeRootDirectory, "bin").FullName;
				string MakePath = DirectoryReference.Combine(TargetPlatform.MakeRootDirectory, "bin").FullName;

				string PrevPath = Environment.GetEnvironmentVariable("PATH");
				// mixing bundled make and cygwin make is no good. Try to detect and remove cygwin paths.
				string PathWithoutCygwin = RemoveOtherMakeAndCygwinFromPath(PrevPath);
				Environment.SetEnvironmentVariable("PATH", CMakePath + ";" + MakePath + ";" + PathWithoutCygwin);
				Environment.SetEnvironmentVariable("PATH", CMakePath + ";" + MakePath + ";" + Environment.GetEnvironmentVariable("PATH"));
				LogInformation("set {0}={1}", "PATH", Environment.GetEnvironmentVariable("PATH"));
			}
		}
	}

	public override void ExecuteBuild()
	{
		bool bAutoCreateChangelist = true;
		if (ParseParam("SkipCreateChangelist"))
		{
			bAutoCreateChangelist = false;
		}

		bool bAutoSubmit = bAutoCreateChangelist;
		if (ParseParam("SkipSubmit"))
		{
			bAutoSubmit = false;
		}

		// if we don't pass anything, we'll just merge by default
		string RobomergeCommand = ParseParamValue("Robomerge", "").ToLower();
		if (!string.IsNullOrEmpty(RobomergeCommand))
		{
			// for merge default action, add flag to make sure buildmachine commit isn't skipped
			if (RobomergeCommand == "merge")
			{
				RobomergeCommand = "#robomerge[all] #DisregardExcludedAuthors";
			}
			// otherwise add hashtags
			else if (RobomergeCommand == "ignore")
			{
				RobomergeCommand = "#robomerge #ignore";
			}
			else if (RobomergeCommand == "null")
			{
				RobomergeCommand = "#robomerge #null";
			}
			// otherwise the submit will likely fail.
			else
			{
				throw new AutomationException("Invalid Robomerge param passed in {0}.  Must be \"merge\", \"null\", or \"ignore\"", RobomergeCommand);
			}
		}

		SetupBuildEnvironment();

		TargetPlatform Platform = GetTargetPlatform();

		TargetLib TargetLib = GetTargetLib();

		List<string> TargetConfigurations = GetTargetConfigurations();

		if (Platform.SeparateProjectPerConfig)
		{
			foreach (string TargetConfiguration in TargetConfigurations)
			{
				Platform.SetupTargetLib(TargetLib, TargetConfiguration);
			}
		}
		else
		{
			Platform.SetupTargetLib(TargetLib, null);
		}

		HashSet<FileReference> FilesToReconcile = new HashSet<FileReference>();
		
		foreach (string TargetConfiguration in TargetConfigurations)
		{
			foreach (FileReference FileToDelete in Platform.EnumerateOutputFiles(TargetLib, TargetConfiguration).Distinct())
			{
				FilesToReconcile.Add(FileToDelete);
	
				// Also clean the output files
				InternalUtils.SafeDeleteFile(FileToDelete.FullName);
			}
		}

		foreach (string TargetConfiguration in TargetConfigurations)
		{
			Platform.BuildTargetLib(TargetLib, TargetConfiguration);
		}

		Platform.CleanupTargetLib(TargetLib, null);

		const int InvalidChangeList = -1;
		int P4ChangeList = InvalidChangeList;

		if (bAutoCreateChangelist)
		{
			string LibDeploymentDesc = TargetLib.Name + " " + Platform.FriendlyName;

			var Builder = new StringBuilder();
			Builder.AppendFormat("BuildCMakeLib.Automation: Deploying {0} libs.{1}", LibDeploymentDesc, Environment.NewLine);
			Builder.AppendLine("#rb none");
			Builder.AppendLine("#lockdown Nick.Penwarden");
			Builder.AppendLine("#tests none");
			Builder.AppendLine("#jira none");
			Builder.AppendLine("#okforgithub ignore");
			if (!string.IsNullOrEmpty(RobomergeCommand))
			{
				Builder.AppendLine(RobomergeCommand);
			}

			P4ChangeList = P4.CreateChange(P4Env.Client, Builder.ToString());
		}

		if (P4ChangeList != InvalidChangeList)
		{
			foreach (FileReference FileToReconcile in FilesToReconcile)
			{
				P4.Reconcile(P4ChangeList, FileToReconcile.FullName);
			}

			if (bAutoSubmit)
			{
				if (!P4.TryDeleteEmptyChange(P4ChangeList))
				{
					LogInformation("Submitting changelist " + P4ChangeList.ToString());
					int SubmittedChangeList = InvalidChangeList;
					P4.Submit(P4ChangeList, out SubmittedChangeList);
				}
				else
				{
					LogInformation("Nothing to submit!");
				}
			}
		}
	}
}

class XcodeTargetPlatform_IOS : BuildCMakeLib.XcodeTargetPlatform
{
	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.IOS;
	public override bool HasBinaries => false;
	public override string DebugDatabaseExtension => null;
	public override string DynamicLibraryExtension => null;
	public override string StaticLibraryExtension => "a";
	public override bool IsPlatformExtension => false;
	public override bool UseResponseFiles => false;
	public override string TargetBuildPlatform => "ios";
	public override bool SupportsTargetLib(BuildCMakeLib.TargetLib Library)
	{
		return true;
	}
}

class MakefileTargetPlatform_IOS : BuildCMakeLib.MakefileTargetPlatform
{
	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.IOS;
	public override bool HasBinaries => false;
	public override string DebugDatabaseExtension => null;
	public override string DynamicLibraryExtension => null;
	public override string StaticLibraryExtension => "a";
	public override bool IsPlatformExtension => false;
	public override bool UseResponseFiles => false;
	public override string TargetBuildPlatform => "ios";
	public override bool SupportsTargetLib(BuildCMakeLib.TargetLib Library)
	{
		return true;
	}
}