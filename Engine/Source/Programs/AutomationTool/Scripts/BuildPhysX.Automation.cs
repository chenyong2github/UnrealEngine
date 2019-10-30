// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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

[Help("Builds PhysX/APEX libraries using CMake build system.")]
[Help("TargetLibs", "Specify a list of target libraries to build, separated by '+' characters (eg. -TargetLibs=PhysX+APEX). Default is PhysX+APEX+NvCloth.")]
[Help("TargetPlatforms", "Specify a list of target platforms to build, separated by '+' characters (eg. -TargetPlatforms=Win32+Win64). Architectures are specified with '-'. Default is Win32+Win64.")]
[Help("TargetConfigs", "Specify a list of configurations to build, separated by '+' characters (eg. -TargetConfigs=profile+debug). Default is profile+release+checked+debug.")]
[Help("SkipBuildSolutions", "Skip generating cmake project files. Existing cmake project files will be used.")]
[Help("SkipBuild", "Do not perform build step. If this argument is not supplied libraries will be built (in accordance with TargetLibs, TargetPlatforms and TargetWindowsCompilers).")]
[Help("SkipCreateChangelist", "Do not create a P4 changelist for source or libs. If this argument is not supplied source and libs will be added to a Perforce changelist.")]
[Help("SkipSubmit", "Do not perform P4 submit of source or libs. If this argument is not supplied source and libs will be automatically submitted to Perforce. If SkipCreateChangelist is specified, this argument applies by default.")]
[Help("Robomerge", "Which robomerge action to apply to the submission. If we're skipping submit, this is not used.")]
[RequireP4]
public sealed class BuildPhysX : BuildCommand
{
	// The libs we can optionally build
	public enum PhysXTargetLib
	{
		PhysX,
		APEX,		// Note: Building APEX deploys shared binaries and libs
        NvCloth
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
		public static DirectoryReference PhysX3RootDirectory = DirectoryReference.Combine(RootDirectory, "Engine/Source/ThirdParty/PhysX3");
		public static DirectoryReference ThirdPartySourceDirectory = DirectoryReference.Combine(RootDirectory, "Engine/Source/ThirdParty");
		public static DirectoryReference PxSharedRootDirectory = DirectoryReference.Combine(PhysX3RootDirectory, "PxShared");

		private DirectoryReference PlatformEngineRoot => IsPlatformExtension
			? DirectoryReference.Combine(RootDirectory, "Engine", "Platforms", Platform.ToString())
			: DirectoryReference.Combine(RootDirectory, "Engine");

		public DirectoryReference OutputBinaryDirectory => DirectoryReference.Combine(PlatformEngineRoot, "Binaries/ThirdParty/PhysX3", IsPlatformExtension ? "" : Platform.ToString(), PlatformBuildSubdirectory ?? "");
		public DirectoryReference OutputLibraryDirectory => DirectoryReference.Combine(PlatformEngineRoot, "Source/ThirdParty/PhysX3/Lib", IsPlatformExtension ? "" : Platform.ToString(), PlatformBuildSubdirectory ?? "");
		private DirectoryReference PxSharedSourceRootDirectory => DirectoryReference.Combine(PlatformEngineRoot, "Source/ThirdParty/PhysX3/PxShared");

		private DirectoryReference GetTargetLibSourceRootDirectory(PhysXTargetLib TargetLib)
		{
			Dictionary<PhysXTargetLib, string> SourcePathMap = new Dictionary<PhysXTargetLib, string>
			{
				{ PhysXTargetLib.PhysX,   "Source/ThirdParty/PhysX3/PhysX_3.4/Source" },
				{ PhysXTargetLib.APEX,    "Source/ThirdParty/PhysX3/APEX_1.4" },
				{ PhysXTargetLib.NvCloth, "Source/ThirdParty/PhysX3/NvCloth" },
			};

			return DirectoryReference.Combine(PlatformEngineRoot, SourcePathMap[TargetLib]);
			}
		
		private DirectoryReference GetCommonCMakeDirectory(PhysXTargetLib TargetLib)
			{
			Dictionary<PhysXTargetLib, string> SourcePathMap = new Dictionary<PhysXTargetLib, string>
		{
				{ PhysXTargetLib.PhysX,   "Engine/Source/ThirdParty/PhysX3/PhysX_3.4/Source/compiler/cmake/common" },
				{ PhysXTargetLib.APEX,    "Engine/Source/ThirdParty/PhysX3/APEX_1.4/compiler/cmake/common" },
				{ PhysXTargetLib.NvCloth, "Engine/Source/ThirdParty/PhysX3/NvCloth/compiler/cmake/common" },
			};

			return DirectoryReference.Combine(RootDirectory, SourcePathMap[TargetLib]);
		}

		protected DirectoryReference GetTargetLibPlatformCMakeDirectory(PhysXTargetLib TargetLib) =>
			DirectoryReference.Combine(GetTargetLibSourceRootDirectory(TargetLib), "compiler", "cmake", IsPlatformExtension ? "" : TargetBuildPlatform);

		protected DirectoryReference GetProjectsDirectory(PhysXTargetLib TargetLib, string TargetConfiguration) =>
			DirectoryReference.Combine(GetTargetLibSourceRootDirectory(TargetLib), "compiler", 
				IsPlatformExtension ? "" : TargetBuildPlatform, 
				PlatformBuildSubdirectory ?? "", 
				SeparateProjectPerConfig ? BuildMap[TargetConfiguration] : "");

		protected FileReference GetToolchainPath(PhysXTargetLib TargetLib, string TargetConfiguration)
		{
			string ToolchainName = GetToolchainName(TargetLib, TargetConfiguration);

			if (ToolchainName == null)
				return null;

			return FileReference.Combine(PlatformEngineRoot, "Source/ThirdParty/PhysX3/Externals/CMakeModules", IsPlatformExtension ? "" : Platform.ToString(), ToolchainName);
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
			: "cmake";

		public virtual Dictionary<string, string> BuildMap => new Dictionary<string, string>()
		{
			{ "debug",   "debug"   },
			{ "checked", "checked" },
			{ "profile", "profile" },
			{ "release", "release" }
		};

		public virtual Dictionary<string, string> BuildSuffix => new Dictionary<string, string>()
	{
			{ "debug",   "DEBUG"   },
			{ "checked", "CHECKED" },
			{ "profile", "PROFILE" },
			{ "release", ""        }
		};

		public abstract bool SupportsTargetLib(PhysXTargetLib Library);

		public virtual string GetToolchainName(PhysXTargetLib TargetLib, string TargetConfiguration) => null;
		public virtual string GetAdditionalCMakeArguments(PhysXTargetLib TargetLib, string TargetConfiguration) => null;

		public virtual string GetCMakeArguments(PhysXTargetLib TargetLib, string TargetConfiguration)
		{
			string Args = "\"" + GetTargetLibPlatformCMakeDirectory(TargetLib).FullName + "\"";
			Args += " -G \"" + CMakeGeneratorName + "\"";
			Args += " -DTARGET_BUILD_PLATFORM=\"" + TargetBuildPlatform + "\"";

			if (SeparateProjectPerConfig)
	{
				Args += " -DCMAKE_BUILD_TYPE=\"" + TargetConfiguration + "\"";
	}

			FileReference ToolchainPath = GetToolchainPath(TargetLib, TargetConfiguration);
			if (ToolchainPath != null)
		{
				Args += " -DCMAKE_TOOLCHAIN_FILE=\"" + ToolchainPath.FullName + "\"";
		}

			Args += " -DPX_OUTPUT_LIB_DIR=\"" + OutputLibraryDirectory + "\"";

			if (HasBinaries)
	{
				Args += " -DPX_OUTPUT_DLL_DIR=\"" + OutputBinaryDirectory + "\"";
				Args += " -DPX_OUTPUT_EXE_DIR=\"" + OutputBinaryDirectory + "\"";
		}

			if (UseResponseFiles)
		{
		// Enable response files for platforms that require them.
		// Response files are used for include paths etc, to fix max command line length issues.
				Args += " -DUSE_RESPONSE_FILES=1";
		}

			if (TargetLib == PhysXTargetLib.APEX)
				{
				Args += " -DAPEX_ENABLE_UE4=1";
				}

			string AdditionalArgs = GetAdditionalCMakeArguments(TargetLib, TargetConfiguration);
			if (AdditionalArgs != null)
				{
				Args += AdditionalArgs;
				}

			return Args;
				}

		public IEnumerable<FileReference> EnumerateOutputFiles(DirectoryReference BaseDir, string SearchPrefix, PhysXTargetLib TargetLib)
					{
			if (!DirectoryReference.Exists(BaseDir))
				yield break;

			Func<string, bool> IsApex    = (f) => f.Contains("APEX") || APEXSpecialLibs.Any(Lib => f.Contains(Lib.ToUpper()));
			Func<string, bool> IsNvCloth = (f) => f.Contains("NVCLOTH");

			foreach (FileReference File in DirectoryReference.EnumerateFiles(BaseDir, SearchPrefix))
					{
				var FileNameUpper = File.GetFileName().ToUpper();

				switch (TargetLib)
				{
			case PhysXTargetLib.APEX:
						if (IsApex(FileNameUpper))
				{
							yield return File;
				}
						break;
					case PhysXTargetLib.NvCloth:
						if (IsNvCloth(FileNameUpper))
				{
							yield return File;
				}
						break;

					case PhysXTargetLib.PhysX:
						if (!IsApex(FileNameUpper) && !IsNvCloth(FileNameUpper))
							yield return File;
						break;

					default:
						throw new ArgumentException("TargetLib");
				}
				}
				}

		public IEnumerable<FileReference> EnumerateOutputFiles(PhysXTargetLib TargetLib, string TargetConfiguration)
				{
			string SearchPrefix = "*" + BuildSuffix[TargetConfiguration] + ".";
			
			// Scan static libraries directory
			IEnumerable<FileReference> Results = EnumerateOutputFiles(OutputLibraryDirectory, SearchPrefix + StaticLibraryExtension, TargetLib);
			if (DebugDatabaseExtension != null)
				{
				Results = Results.Concat(EnumerateOutputFiles(OutputLibraryDirectory, SearchPrefix + DebugDatabaseExtension, TargetLib));
				}

			// Scan dynamic libraries directory
			if (HasBinaries)
				{
				Results = Results.Concat(EnumerateOutputFiles(OutputBinaryDirectory, SearchPrefix + DynamicLibraryExtension, TargetLib));
				if (DebugDatabaseExtension != null)
				{
					Results = Results.Concat(EnumerateOutputFiles(OutputBinaryDirectory, SearchPrefix + DebugDatabaseExtension, TargetLib));
				}
				}

			return Results;
				}

		public virtual void SetupTargetLib(PhysXTargetLib TargetLib, string TargetConfiguration)
				{
			// make sure we set up the environment variable specifying where the root of the PhysX SDK is
			Environment.SetEnvironmentVariable("GW_DEPS_ROOT", PhysX3RootDirectory.FullName.Replace('\\', '/'));
			LogInformation("set {0}={1}", "GW_DEPS_ROOT", Environment.GetEnvironmentVariable("GW_DEPS_ROOT"));
			Environment.SetEnvironmentVariable("CMAKE_MODULE_PATH", DirectoryReference.Combine(PhysX3RootDirectory, "Externals", "CMakeModules").FullName.Replace('\\', '/'));
			LogInformation("set {0}={1}", "CMAKE_MODULE_PATH", Environment.GetEnvironmentVariable("CMAKE_MODULE_PATH"));

			if (BuildHostPlatform.Current.Platform.IsInGroup(UnrealPlatformGroup.Unix))
				{
				Environment.SetEnvironmentVariable("CMAKE_ROOT", DirectoryReference.Combine(CMakeRootDirectory, "share").FullName);
				LogInformation("set {0}={1}", "CMAKE_ROOT", Environment.GetEnvironmentVariable("CMAKE_ROOT"));
				}

			DirectoryReference CMakeTargetDirectory = GetProjectsDirectory(TargetLib, TargetConfiguration);
			MakeFreshDirectoryIfRequired(CMakeTargetDirectory);

			LogInformation("Generating projects for lib " + TargetLib.ToString() + ", " + FriendlyName);

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.FileName = CMakeCommand;
			StartInfo.WorkingDirectory = CMakeTargetDirectory.FullName;
			StartInfo.Arguments = GetCMakeArguments(TargetLib, TargetConfiguration);

			if (Utils.RunLocalProcessAndLogOutput(StartInfo) != 0)
				{
				throw new AutomationException("Unable to generate projects for {0}.", TargetLib.ToString() + ", " + FriendlyName);
				}
				}

		public abstract void BuildTargetLib(PhysXTargetLib TargetLib, string TargetConfiguration);
	}

	public abstract class MSBuildTargetPlatform : TargetPlatform
	{
		// We cache our own MSDev and MSBuild executables
		private string MsDevExe;
		private string MsBuildExe;

		public string CompilerName { get; private set; }
		public WindowsCompiler Compiler { get; private set; }
		public string VisualStudioName { get; private set; }

		public override string CMakeGeneratorName => VisualStudioName;
		public override string FriendlyName => Platform.ToString() + "-" + CompilerName;

		public override bool SeparateProjectPerConfig => false;

		public MSBuildTargetPlatform(string CompilerName = "VS2015")
		{
			this.CompilerName = CompilerName;
			switch (CompilerName)
		{
				case "VS2015":
					Compiler = WindowsCompiler.VisualStudio2015_DEPRECATED;
					VisualStudioName = "Visual Studio 14 2015";
					break;
				case "VS2017":
					Compiler = WindowsCompiler.VisualStudio2017;
					VisualStudioName = "Visual Studio 15 2017";
					break;
				case "VS2019":
					Compiler = WindowsCompiler.VisualStudio2019;
					VisualStudioName = "Visual Studio 16 2019";
					break;
				default:
					throw new BuildException("Unknown windows compiler specified: {0}", CompilerName);
		}

			DirectoryReference VSPath;
			if (!WindowsExports.TryGetVSInstallDir(Compiler, out VSPath))
				throw new BuildException("Failed to get Visual Studio install directory");

			MsDevExe = FileReference.Combine(VSPath, "Common7", "IDE", "Devenv.com").FullName;
			MsBuildExe = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "MSBuild", "14.0", "Bin", "MSBuild.exe");
	}

		public virtual string GetMsDevCommandArgs(string SolutionFile, string TargetConfiguration)
		{
			return string.Format("\"{0}\" /build \"{1}\"", SolutionFile, TargetConfiguration);
		}

		public override void BuildTargetLib(PhysXTargetLib TargetLib, string TargetConfiguration)
	{
			string SolutionName;
		switch (TargetLib)
		{
				case PhysXTargetLib.PhysX: SolutionName = "PhysX.sln"; break;
				case PhysXTargetLib.APEX: SolutionName = "APEX.sln"; break;
				case PhysXTargetLib.NvCloth: SolutionName = "NvCloth.sln"; break;
			default:
					throw new ArgumentException("TargetLib");
	}

			string SolutionFile = FileReference.Combine(GetProjectsDirectory(TargetLib, TargetConfiguration), SolutionName).FullName;
			if (!FileExists(SolutionFile))
	{
				throw new AutomationException("Unabled to build Solution {0}. Solution file not found.", SolutionFile);
	}

			RunAndLog(CmdEnv, MsDevExe, GetMsDevCommandArgs(SolutionFile, TargetConfiguration));
		}
	}

	public abstract class MakefileTargetPlatform : TargetPlatform
	{
		// FIXME: use absolute path
		public virtual string MakeCommand => "make";

		// FIXME: "j -16" should be tweakable
		//string MakeOptions = "-j 1 VERBOSE=1";
		public virtual string MakeOptions => "-j 16";

		public override bool SeparateProjectPerConfig => true;

		public override string CMakeGeneratorName => "Unix Makefiles";

		public override void BuildTargetLib(PhysXTargetLib TargetLib, string TargetConfiguration)
	{
			DirectoryReference ConfigDirectory = GetProjectsDirectory(TargetLib, TargetConfiguration);
			Environment.SetEnvironmentVariable("LIB_SUFFIX", BuildSuffix[TargetConfiguration]);

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
				? string.Format("{1} \"MAKE={0} {1}\"", MakeCommand, MakeOptions)
				: MakeOptions;

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
		public override string CMakeCommand => FileReference.Combine(CMakeRootDirectory, "bin", "cmake").FullName;

		public override string CMakeGeneratorName => "Xcode";

		public override bool SeparateProjectPerConfig => false;

		public override void BuildTargetLib(PhysXTargetLib TargetLib, string TargetConfiguration)
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

	// Apex libs that do not have an APEX prefix in their name
	private static string[] APEXSpecialLibs = { "NvParameterized", "RenderDebug" };

	private List<PhysXTargetLib> GetTargetLibs()
	{
		List<PhysXTargetLib> TargetLibs = new List<PhysXTargetLib>();
		string TargetLibsFilter = ParseParamValue("TargetLibs", "PhysX+APEX+NvCloth");
		if (TargetLibsFilter != null)
		{
			foreach (string TargetLibName in TargetLibsFilter.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries))
			{
				PhysXTargetLib TargetLib;
				if (!Enum.TryParse(TargetLibName, out TargetLib))
				{
					throw new AutomationException("Unknown target lib '{0}' specified on command line", TargetLibName);
				}
				else
				{
					TargetLibs.Add(TargetLib);
				}
			}
		}
		return TargetLibs;
	}

	public List<string> GetTargetConfigurations()
	{
		List<string> TargetConfigs = new List<string>();
		// Remove any configs that aren't enabled on the command line
		string TargetConfigFilter = ParseParamValue("TargetConfigs", "profile+release+checked+debug");
		if (TargetConfigFilter != null)
		{
			foreach (string TargetConfig in TargetConfigFilter.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries))
			{
				TargetConfigs.Add(TargetConfig);
			}
		}

		return TargetConfigs;
	}

	private List<TargetPlatform> GetTargetPlatforms()
	{
		var TargetPlatforms = new Dictionary<string, TargetPlatform>();

		// Grab all the non-abstract subclasses of TargetPlatform from the executing assembly.
		var AvailablePlatformTypes = from Assembly in AppDomain.CurrentDomain.GetAssemblies()
									 from Type in Assembly.GetTypes()
									 where !Type.IsAbstract && Type.IsSubclassOf(typeof(TargetPlatform))
									 select Type;

		var PlatformTypeMap = new Dictionary<string, Type>();

		foreach (var Type in AvailablePlatformTypes)
				{
			int Index = Type.Name.LastIndexOf('_');
			if (Index == -1)
			{
				throw new BuildException("Invalid PhysX target platform type found: {0}", Type);
		}

			string PlatformName = Type.Name.Substring(Index + 1);
			PlatformTypeMap.Add(PlatformName, Type);
	}

		// Remove any platforms that aren't enabled on the command line
		string TargetPlatformFilter = ParseParamValue("TargetPlatforms", "Win32+Win64");
		if (TargetPlatformFilter != null)
	{
			foreach (string TargetPlatformName in TargetPlatformFilter.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries))
	{
				// Split the name on '-' and pass all of them minus the first one as arguments to the platform type constructor
				var SelectedPlatform = TargetPlatformName;
				string PlatformArgString = null;

				int DashIndex = TargetPlatformName.IndexOf('-');
				if (DashIndex != -1)
		{
					SelectedPlatform = TargetPlatformName.Substring(0, DashIndex);
					PlatformArgString = TargetPlatformName.Substring(DashIndex + 1);
				}

				if (TargetPlatforms.ContainsKey(TargetPlatformName))
				{
					// Ignore duplicate instances of the same target platform and arg
					continue;
		}

				if (!PlatformTypeMap.ContainsKey(SelectedPlatform))
			{
					throw new BuildException("Unknown PhysX target platform specified: {0}", SelectedPlatform);
			}

				var SelectedType = PlatformTypeMap[SelectedPlatform];
				var Constructors = SelectedType.GetConstructors();
				if (Constructors.Length != 1)
					{
					throw new BuildException("PhysX build platform implementation type \"{0}\" should have exactly one constructor.", SelectedType);
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

				var Args = new object[Parameters.Length];
				if (Args.Length > 0)
			{
					if (PlatformArgString == null)
				{
						if (!Parameters[0].HasDefaultValue)
					{
							throw new BuildException("Missing a required argument in the target platform name \"{0}\".", TargetPlatformName);
				}
						else
						{
							Args[0] = Parameters[0].DefaultValue;
			}
		}
		else
		{
						Args[0] = PlatformArgString;
			}
		}
				else if (PlatformArgString != null)
				{
					throw new BuildException("Unnecessary option passed as part of the target platform name \"{0}\".", TargetPlatformName);
	}

				var Instance = (TargetPlatform)Activator.CreateInstance(SelectedType, Args);

				TargetPlatforms.Add(TargetPlatformName, Instance);
		}
	}

		return TargetPlatforms.Values.ToList();
	}

	private static string RemoveOtherMakeAndCygwinFromPath(string WindowsPath)
	{
		string[] PathComponents = WindowsPath.Split(';');
		string NewPath = "";
		foreach(string PathComponent in PathComponents)
		{
			// everything what contains /bin or /sbin is suspicious, check if it has make in it
			if (PathComponent.Contains("\\bin") || PathComponent.Contains("/bin") || PathComponent.Contains("\\sbin") || PathComponent.Contains("/sbin"))
			{
				if (File.Exists(PathComponent + "/make.exe") || File.Exists(PathComponent + "make.exe") || File.Exists(PathComponent + "/cygwin1.dll"))
				{
					// gotcha!
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
				DirectoryReference ThirdPartyNotUERootDirectory = DirectoryReference.Combine(RootDirectory, "Engine/Extras/ThirdPartyNotUE");
				string CMakePath = DirectoryReference.Combine(ThirdPartyNotUERootDirectory, "CMake/bin").FullName;
				string MakePath = DirectoryReference.Combine(ThirdPartyNotUERootDirectory, "GNU_Make/make-3.81/bin").FullName;

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
		bool bBuildSolutions = true;
		if (ParseParam("SkipBuildSolutions"))
		{
			bBuildSolutions = false;
		}

		bool bBuildLibraries = true;
		if (ParseParam("SkipBuild"))
			{
			bBuildLibraries = false;
	}

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

		// get the platforms we want to build for
		List<TargetPlatform> TargetPlatforms = GetTargetPlatforms();

		// get the configurations we want to build for
		List<string> TargetConfigurations = GetTargetConfigurations();

		// Parse out the libs we want to build
		List<PhysXTargetLib> TargetLibs = GetTargetLibs();

		if (bBuildSolutions)
		{
			foreach (PhysXTargetLib TargetLib in TargetLibs)
		{
				// build target lib for all platforms
				foreach (TargetPlatform Platform in TargetPlatforms.Where(P => P.SupportsTargetLib(TargetLib)))
    {
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
        }
        }
    }

		HashSet<FileReference> FilesToReconcile = new HashSet<FileReference>();
		if (bBuildLibraries)
		{
			// Compile the list of all files to reconcile
			foreach (PhysXTargetLib TargetLib in TargetLibs)
	{
				foreach (TargetPlatform Platform in TargetPlatforms.Where(P => P.SupportsTargetLib(TargetLib)))
		{
					foreach (string TargetConfiguration in TargetConfigurations)
		{
						foreach (FileReference FileToDelete in Platform.EnumerateOutputFiles(TargetLib, TargetConfiguration).Distinct())
		{
							FilesToReconcile.Add(FileToDelete);
	
							// Also clean the output files
							InternalUtils.SafeDeleteFile(FileToDelete.FullName);
						}
			}
		}
		}

			// Build each target lib, for each config and platform
			foreach (PhysXTargetLib TargetLib in TargetLibs)
		{
				foreach (TargetPlatform Platform in TargetPlatforms.Where(P => P.SupportsTargetLib(TargetLib)))
				{
					foreach (string TargetConfiguration in TargetConfigurations)
			{
						Platform.BuildTargetLib(TargetLib, TargetConfiguration);
			}
			}
		}
	}

		const int InvalidChangeList = -1;
		int P4ChangeList = InvalidChangeList;

		if (bAutoCreateChangelist)
	{
			string LibDeploymentDesc = "";

			foreach (PhysXTargetLib Lib in TargetLibs)
			{
				if (LibDeploymentDesc.Length != 0)
        {
					LibDeploymentDesc += " & ";
        }

				LibDeploymentDesc += Lib.ToString();
			}

			foreach (TargetPlatform TargetData in TargetPlatforms)
		{
				LibDeploymentDesc += " " + TargetData.FriendlyName;
		}

			var Builder = new StringBuilder();
			Builder.AppendFormat("BuildPhysX.Automation: Deploying {0} libs.{1}", LibDeploymentDesc, Environment.NewLine);
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
			foreach (PhysXTargetLib TargetLib in TargetLibs)
		{
				foreach (string TargetConfiguration in TargetConfigurations)
			{
					//Add any new files that p4 is not yet tracking.
					foreach (TargetPlatform Platform in TargetPlatforms)
				{
						if (!Platform.SupportsTargetLib(TargetLib))
			{
							continue;
			}

						foreach (var File in Platform.EnumerateOutputFiles(TargetLib, TargetConfiguration))
			{
							FilesToReconcile.Add(File);
			}
			}
		}
		}

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

class BuildPhysX_Android : BuildPhysX.MakefileTargetPlatform
		{
	public BuildPhysX_Android(string Architecture)
		{
		this.Architecture = Architecture;
		}

	public string Architecture { get; private set; }

	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.Android;
	public override string PlatformBuildSubdirectory => Architecture;
	public override bool HasBinaries => false;
	public override string DebugDatabaseExtension => null;
	public override string DynamicLibraryExtension => null;
	public override string StaticLibraryExtension => "a";
	public override bool IsPlatformExtension => false;
	public override bool UseResponseFiles => false;
	public override string TargetBuildPlatform => "android";
	public override string CMakeGeneratorName => "MinGW Makefiles";
	public override string FriendlyName => Platform.ToString() + "-" + Architecture;

	public override string MakeCommand
		{
		get
			{
			// Use make from Android toolchain
			string NDKDirectory = Environment.GetEnvironmentVariable("NDKROOT");

			// don't register if we don't have an NDKROOT specified
			if (string.IsNullOrEmpty(NDKDirectory))
		{
				throw new AutomationException("NDKROOT is not specified; cannot build Android.");
		}

			NDKDirectory = NDKDirectory.Replace("\"", "");

			return NDKDirectory + "\\prebuilt\\windows-x86_64\\bin\\make.exe";
		}
		}

	public override bool SupportsTargetLib(BuildPhysX.PhysXTargetLib Library)
		{
		switch (Library)
		{
			case BuildPhysX.PhysXTargetLib.APEX: return false;
			case BuildPhysX.PhysXTargetLib.NvCloth: return false;
			case BuildPhysX.PhysXTargetLib.PhysX: return true;
			default: return false;
		}
		}

	public override string GetToolchainName(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration) => "android.toolchain.cmake";

	public override string GetAdditionalCMakeArguments(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration)
		{
		string NDKDirectory = Environment.GetEnvironmentVariable("NDKROOT");

		// don't register if we don't have an NDKROOT specified
		if (string.IsNullOrEmpty(NDKDirectory))
			{
			throw new AutomationException("NDKROOT is not specified; cannot build Android.");
			}

		NDKDirectory = NDKDirectory.Replace("\"", "");

		string AndroidAPILevel = "android-19";
		string AndroidABI = "armeabi-v7a";
		switch (Architecture)
		{
			case "armv7": AndroidAPILevel = "android-19"; AndroidABI = "armeabi-v7a"; break;
			case "arm64": AndroidAPILevel = "android-21"; AndroidABI = "arm64-v8a";   break;
			case "x86":   AndroidAPILevel = "android-19"; AndroidABI = "x86";         break;
			case "x64":   AndroidAPILevel = "android-21"; AndroidABI = "x86_64";      break;
		}
		return " -DANDROID_NDK=\"" + NDKDirectory + "\" -DCMAKE_MAKE_PROGRAM=\"" + NDKDirectory + "\\prebuilt\\windows-x86_64\\bin\\make.exe\" -DANDROID_NATIVE_API_LEVEL=\"" + AndroidAPILevel + "\" -DANDROID_ABI=\"" + AndroidABI + "\" -DANDROID_STL=gnustl_shared";
		}
		}

class BuildPhysX_IOS : BuildPhysX.XcodeTargetPlatform
	{
	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.IOS;
	public override bool HasBinaries => false;
	public override string DebugDatabaseExtension => null;
	public override string DynamicLibraryExtension => null;
	public override string StaticLibraryExtension => "a";
	public override bool IsPlatformExtension => false;
	public override bool UseResponseFiles => false;
	public override string TargetBuildPlatform => "ios";

	public override bool SupportsTargetLib(BuildPhysX.PhysXTargetLib Library)
		{
		switch (Library)
		{
			case BuildPhysX.PhysXTargetLib.APEX: return false;
			case BuildPhysX.PhysXTargetLib.NvCloth: return false;
			case BuildPhysX.PhysXTargetLib.PhysX: return true;
			default: return false;
		}
		}
	}

class BuildPhysX_Linux : BuildPhysX.MakefileTargetPlatform
	{
	public BuildPhysX_Linux(string Architecture = "x86_64-unknown-linux-gnu")
		{
		this.Architecture = Architecture;
		}

	private static DirectoryReference DumpSymsPath = DirectoryReference.Combine(RootDirectory, "Engine/Binaries/Linux/dump_syms");
	private static DirectoryReference BreakpadSymbolEncoderPath = DirectoryReference.Combine(RootDirectory, "Engine/Binaries/Linux/BreakpadSymbolEncoder");

	public string Architecture { get; private set; }

	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.Linux;
	public override string PlatformBuildSubdirectory => Architecture;
	public override bool HasBinaries => true;
	public override string DebugDatabaseExtension => null;
	public override string DynamicLibraryExtension => "so";
	public override string StaticLibraryExtension => "a";
	public override bool IsPlatformExtension => false;
	public override bool UseResponseFiles => true;
	public override string TargetBuildPlatform => "linux";
	public override string FriendlyName => Platform.ToString() + "-" + Architecture;

	public override bool SupportsTargetLib(BuildPhysX.PhysXTargetLib Library)
    {
		bool b64BitX86 = Architecture.StartsWith("x86_64");
		switch (Library)
        {
			case BuildPhysX.PhysXTargetLib.APEX: return b64BitX86;
			case BuildPhysX.PhysXTargetLib.NvCloth: return b64BitX86;
			case BuildPhysX.PhysXTargetLib.PhysX: return true;
			default: return false;
        }
    }

	private string GetBundledLinuxLibCxxFlags()
    {
		string CxxFlags = "\"-I " + ThirdPartySourceDirectory + "/Linux/LibCxx/include -I " + ThirdPartySourceDirectory + "/Linux/LibCxx/include/c++/v1\"";
		string CxxLinkerFlags = "\"-stdlib=libc++ -nodefaultlibs -Wl,--build-id -L " 
			+ ThirdPartySourceDirectory + "/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/ " 
			+ ThirdPartySourceDirectory + "/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++.a " 
			+ ThirdPartySourceDirectory + "/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++abi.a -lm -lc -lgcc_s\"";

		return "-DCMAKE_CXX_FLAGS=" + CxxFlags + " -DCMAKE_EXE_LINKER_FLAGS=" + CxxLinkerFlags + " -DCAMKE_MODULE_LINKER_FLAGS=" + CxxLinkerFlags + " -DCMAKE_SHARED_LINKER_FLAGS=" + CxxLinkerFlags + " ";
    }
    
	public override string GetToolchainName(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration)
	{
		// in native builds we don't really use a crosstoolchain description, just use system compiler
		if (BuildHostPlatform.Current.Platform.IsInGroup(UnrealPlatformGroup.Unix))
			return null;

		// otherwise, use a per-architecture file.
		return "LinuxCrossToolchain.multiarch.cmake";
		}

	public override string GetAdditionalCMakeArguments(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration)
		{
		string ToolchainSettings = GetToolchainName(TargetLib, TargetConfiguration) == null
			? " -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
			: " -DARCHITECTURE_TRIPLE=" + Architecture;

		string BundledLinuxLibCxxFlags = GetBundledLinuxLibCxxFlags();

		string Args = " --no-warn-unused-cli -DPX_STATIC_LIBRARIES=1 " + BundledLinuxLibCxxFlags + ToolchainSettings;

		if (TargetLib == BuildPhysX.PhysXTargetLib.APEX)
			{
			Args += " -DAPEX_LINUX_SHARED_LIBRARIES=1";
		}

		return Args;
	}

	public override void SetupTargetLib(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration)
	{
		// the libraries are broken when compiled with clang 7.0.1
		string OriginalToolchainPath = Environment.GetEnvironmentVariable("LINUX_MULTIARCH_ROOT");
		if (!string.IsNullOrEmpty(OriginalToolchainPath))
		{
			string ToolchainPathToUse = OriginalToolchainPath.Replace("v15_clang-8.0.1-centos7", "v12_clang-6.0.1-centos7");
			LogInformation("Working around problems with newer clangs: {0} -> {1}", OriginalToolchainPath, ToolchainPathToUse);
			Environment.SetEnvironmentVariable("LINUX_MULTIARCH_ROOT", ToolchainPathToUse);
		}
		else
		{
			LogWarning("LINUX_MULTIARCH_ROOT is not set!");
		}
			
		base.SetupTargetLib(TargetLib, TargetConfiguration);
		}

	public override void BuildTargetLib(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration)
		{
		base.BuildTargetLib(TargetLib, TargetConfiguration);

		foreach (FileReference SOFile in EnumerateOutputFiles(OutputBinaryDirectory, string.Format("*{0}.{1}", BuildSuffix[TargetConfiguration], DebugDatabaseExtension), TargetLib))
			{
				string ExeSuffix = "";
			if (BuildHostPlatform.Current.Platform.IsInGroup(UnrealPlatformGroup.Windows))
				{
					ExeSuffix += ".exe";
				}

				FileReference PSymbolFile = FileReference.Combine(SOFile.Directory, SOFile.GetFileNameWithoutExtension() + ".psym");
				FileReference SymbolFile = FileReference.Combine(SOFile.Directory, SOFile.GetFileNameWithoutExtension() + ".sym");

				// dump_syms
				ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.FileName = DumpSymsPath.FullName + ExeSuffix;
			StartInfo.Arguments = SOFile.FullName + " " + PSymbolFile.FullName;
				StartInfo.RedirectStandardError = true;

				LogInformation("Running: '{0} {1}'", StartInfo.FileName, StartInfo.Arguments);

			Utils.RunLocalProcessAndLogOutput(StartInfo);

				// BreakpadSymbolEncoder
			StartInfo.FileName = BreakpadSymbolEncoderPath.FullName + ExeSuffix;
			StartInfo.Arguments = PSymbolFile.FullName + " " + SymbolFile.FullName;

				LogInformation("Running: '{0} {1}'", StartInfo.FileName, StartInfo.Arguments);

			Utils.RunLocalProcessAndLogOutput(StartInfo);

				// Clean up the Temp *.psym file, as they are no longer needed
			InternalUtils.SafeDeleteFile(PSymbolFile.FullName);
			}
		}
	}

class BuildPhysX_Mac : BuildPhysX.XcodeTargetPlatform
		{
	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.Mac;
	public override bool HasBinaries => true;
	public override string DebugDatabaseExtension => null;
	public override string DynamicLibraryExtension => "dylib";
	public override string StaticLibraryExtension => "a";
	public override bool IsPlatformExtension => false;
	public override bool UseResponseFiles => false;
	public override string TargetBuildPlatform => "mac";

	public override bool SupportsTargetLib(BuildPhysX.PhysXTargetLib Library)
		{
		switch (Library)
			{
			case BuildPhysX.PhysXTargetLib.APEX: return true;
			case BuildPhysX.PhysXTargetLib.NvCloth: return true;
			case BuildPhysX.PhysXTargetLib.PhysX: return true;
			default: return false;
		}
		}
	}

class BuildPhysX_Switch : BuildPhysX.MSBuildTargetPlatform
		{
	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.Switch;
	public override bool HasBinaries => false;
	public override string DebugDatabaseExtension => null;
	public override string DynamicLibraryExtension => null;
	public override string StaticLibraryExtension => "a";
	public override bool IsPlatformExtension => false;
	public override bool UseResponseFiles => true;
	public override string TargetBuildPlatform => "switch";

	public override bool SupportsTargetLib(BuildPhysX.PhysXTargetLib Library)
		{
		switch (Library)
			{
			case BuildPhysX.PhysXTargetLib.APEX: return true;
			case BuildPhysX.PhysXTargetLib.NvCloth: return true;
			case BuildPhysX.PhysXTargetLib.PhysX: return true;
			default: return false;
			}
		}

	public override string GetToolchainName(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration) => "NX64Toolchain.txt";
	public override string GetAdditionalCMakeArguments(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration) => " -DCMAKE_GENERATOR_PLATFORM=NX-NXFP2-a64";

	public override string GetMsDevCommandArgs(string SolutionFile, string TargetConfiguration)
		{
		string AdditionalProperties = "";

		string AutoSDKPropsPath = Environment.GetEnvironmentVariable("SwitchAutoSDKProp");
		if (AutoSDKPropsPath != null && AutoSDKPropsPath.Length > 0)
			{
			AdditionalProperties += string.Format(";CustomBeforeMicrosoftCommonProps={0}", AutoSDKPropsPath);
			}

		FileReference SwitchCMakeModulesPath = FileReference.Combine(PhysX3RootDirectory, "Externals/CMakeModules/Switch/Microsoft.Cpp.NX-NXFP2-a64.user.props");
		if (FileReference.Exists(SwitchCMakeModulesPath))
			{
			AdditionalProperties += string.Format(";ForceImportBeforeCppTargets={0}", SwitchCMakeModulesPath);
			}

		return string.Format("\"{0}\" /t:build /p:Configuration={1};Platform=NX-NXFP2-a64{2}", SolutionFile, TargetConfiguration, AdditionalProperties);
		}
	}

class BuildPhysX_TVOS : BuildPhysX.XcodeTargetPlatform
	{
	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.TVOS;
	public override bool HasBinaries => false;
	public override string DebugDatabaseExtension => null;
	public override string DynamicLibraryExtension => null;
	public override string StaticLibraryExtension => "a";
	public override bool IsPlatformExtension => false;
	public override bool UseResponseFiles => false;
	public override string TargetBuildPlatform => "tvos";

	public override bool SupportsTargetLib(BuildPhysX.PhysXTargetLib Library)
	{
		switch (Library)
		{
			case BuildPhysX.PhysXTargetLib.APEX: return false;
			case BuildPhysX.PhysXTargetLib.NvCloth: return false;
			case BuildPhysX.PhysXTargetLib.PhysX: return true;
			default: return false;
		}
		}
		}

abstract class BuildPhysX_WindowsCommon : BuildPhysX.MSBuildTargetPlatform
		{
	public BuildPhysX_WindowsCommon(string CompilerName)
		: base(CompilerName)
	{ }

	public override string PlatformBuildSubdirectory => CompilerName;
	public override string TargetBuildPlatform => "windows";
	public override bool HasBinaries => true;
	public override string DebugDatabaseExtension => "pdb";
	public override string DynamicLibraryExtension => "dll";
	public override string StaticLibraryExtension => "lib";
	public override bool IsPlatformExtension => false;
	public override bool UseResponseFiles => false;
		
	public override bool SupportsTargetLib(BuildPhysX.PhysXTargetLib Library)
			{
		switch (Library)
			{
			case BuildPhysX.PhysXTargetLib.APEX: return true;
			case BuildPhysX.PhysXTargetLib.NvCloth: return true;
			case BuildPhysX.PhysXTargetLib.PhysX: return true;
			default: return false;
			}
			}
		}

class BuildPhysX_Win32 : BuildPhysX_WindowsCommon
			{
	public BuildPhysX_Win32(string Compiler = "VS2015")
		: base(Compiler)
	{ }

	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.Win32;

	public override Dictionary<string, string> BuildSuffix => new Dictionary<string, string>()
		{
		{ "debug",   "DEBUG_x86"   },
		{ "checked", "CHECKED_x86" },
		{ "profile", "PROFILE_x86" },
		{ "release", "_x86"        }
	};

	public override string GetAdditionalCMakeArguments(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration)
					{
		switch (TargetLib)
						{
			case BuildPhysX.PhysXTargetLib.APEX:    return " -AWin32";
			case BuildPhysX.PhysXTargetLib.NvCloth: return " -AWin32 -DNV_CLOTH_ENABLE_CUDA=0 -DNV_CLOTH_ENABLE_DX11=0";
			case BuildPhysX.PhysXTargetLib.PhysX:   return " -AWin32";
			default: throw new ArgumentException("TargetLib");
							}
						}
						}

class BuildPhysX_Win64 : BuildPhysX_WindowsCommon
					{
	public BuildPhysX_Win64(string Compiler = "VS2015")
		: base(Compiler)
	{ }

	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.Win64;

	public override Dictionary<string, string> BuildSuffix => new Dictionary<string, string>()
					{
		{ "debug",   "DEBUG_x64"   },
		{ "checked", "CHECKED_x64" },
		{ "profile", "PROFILE_x64" },
		{ "release", "_x64"        }
	};

	public override string GetAdditionalCMakeArguments(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration)
		{
		switch (TargetLib)
			{
			case BuildPhysX.PhysXTargetLib.APEX:    return " -Ax64";
			case BuildPhysX.PhysXTargetLib.NvCloth: return " -Ax64 -DNV_CLOTH_ENABLE_CUDA=0 -DNV_CLOTH_ENABLE_DX11=0";
			case BuildPhysX.PhysXTargetLib.PhysX:   return " -Ax64";
			default: throw new ArgumentException("TargetLib");
		}
			}
			}

class BuildPhysX_XboxOne : BuildPhysX.MSBuildTargetPlatform
			{
	public override UnrealTargetPlatform Platform => UnrealTargetPlatform.XboxOne;
	public override bool HasBinaries => false;
	public override string DebugDatabaseExtension => "pdb";
	public override string DynamicLibraryExtension => null;
	public override string StaticLibraryExtension => "lib";
	public override bool IsPlatformExtension => false;
	public override bool UseResponseFiles => false;
	public override string TargetBuildPlatform => "xboxone";
	public override string GetToolchainName(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration) => "XboxOneToolchain.txt";
	public override string GetAdditionalCMakeArguments(BuildPhysX.PhysXTargetLib TargetLib, string TargetConfiguration) => " -DCMAKE_GENERATOR_PLATFORM=DURANGO";

	public override bool SupportsTargetLib(BuildPhysX.PhysXTargetLib Library)
	{
		switch (Library)
			{
			case BuildPhysX.PhysXTargetLib.APEX: return true;
			case BuildPhysX.PhysXTargetLib.NvCloth: return true;
			case BuildPhysX.PhysXTargetLib.PhysX: return true;
			default: return false;
			}
		}

	public override string GetMsDevCommandArgs(string SolutionFile, string TargetConfiguration)
		{
		string AdditionalProperties = "";

		string AutoSDKPropsPath = Environment.GetEnvironmentVariable("XboxOneAutoSDKProp");
		if (AutoSDKPropsPath != null && AutoSDKPropsPath.Length > 0)
						{
			AdditionalProperties += string.Format(";CustomBeforeMicrosoftCommonProps={0}", AutoSDKPropsPath);
			}

		FileReference XboxCMakeModulesPath = FileReference.Combine(PhysX3RootDirectory, "Externals/CMakeModules/XboxOne/Microsoft.Cpp.Durango.user.props");
		if (FileReference.Exists(XboxCMakeModulesPath))
			{
			AdditionalProperties += string.Format(";ForceImportBeforeCppTargets={0}", XboxCMakeModulesPath);
		}

		return string.Format("\"{0}\" /t:build /p:Configuration={1};Platform=Durango{2}", SolutionFile, TargetConfiguration, AdditionalProperties);
	}
}
