// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;
using EpicGames.Core;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	static class UnrealBuildTool
	{
		/// <summary>
		/// Save the application startup time. This can be used as the timestamp for build makefiles, to determine a base time after which any
		/// modifications should invalidate it.
		/// </summary>
		static public DateTime StartTimeUtc { get; } = DateTime.UtcNow;

		/// <summary>
		/// The environment at boot time.
		/// </summary>
		static public System.Collections.IDictionary? InitialEnvironment;

		/// <summary>
		/// Whether we're running with an installed project
		/// </summary>
		static private bool? bIsProjectInstalled;

		/// <summary>
		/// If we are running with an installed project, specifies the path to it
		/// </summary>
		static FileReference? InstalledProjectFile;

		/// <summary>
		/// Directory for saved application settings (typically Engine/Programs)
		/// </summary>
		static DirectoryReference? CachedEngineProgramSavedDirectory;

		/// <summary>
		/// The full name of the Engine/Source directory
		/// </summary>
		public static readonly DirectoryReference EngineSourceDirectory = DirectoryReference.Combine(Unreal.EngineDirectory, "Source");

		/// <summary>
		/// Full path to the Engine/Source/Runtime directory
		/// </summary>
		[Obsolete("Please use UnrealBuildTool.GetExtensionDirs(Unreal.EngineDirectory, \"Source/Runtime\") instead.")]
		public static readonly DirectoryReference EngineSourceRuntimeDirectory = DirectoryReference.Combine(EngineSourceDirectory, "Runtime");

		/// <summary>
		/// Full path to the Engine/Source/Developer directory
		/// </summary>
		[Obsolete("Please use UnrealBuildTool.GetExtensionDirs(Unreal.EngineDirectory, \"Source/Developer\") instead.")]
		public static readonly DirectoryReference EngineSourceDeveloperDirectory = DirectoryReference.Combine(EngineSourceDirectory, "Developer");

		/// <summary>
		/// Full path to the Engine/Source/Editor directory
		/// </summary>
		[Obsolete("Please use UnrealBuildTool.GetExtensionDirs(Unreal.EngineDirectory, \"Source/Editor\") instead.")]
		public static readonly DirectoryReference EngineSourceEditorDirectory = DirectoryReference.Combine(EngineSourceDirectory, "Editor");

		/// <summary>
		/// Full path to the Engine/Source/Programs directory
		/// </summary>
		[Obsolete("Please use UnrealBuildTool.GetExtensionDirs(Unreal.EngineDirectory, \"Source/Programs\") instead.")]
		public static readonly DirectoryReference EngineSourceProgramsDirectory = DirectoryReference.Combine(EngineSourceDirectory, "Programs");

		/// <summary>
		/// Full path to the Engine/Source/ThirdParty directory
		/// </summary>
		[Obsolete("Please use UnrealBuildTool.GetExtensionDirs(Unreal.EngineDirectory, \"Source/ThirdParty\") instead.")]
		public static readonly DirectoryReference EngineSourceThirdPartyDirectory = DirectoryReference.Combine(EngineSourceDirectory, "ThirdParty");

		/// <summary>
		/// Cached copy of the writable engine directory
		/// </summary>
		static DirectoryReference? CachedWritableEngineDirectory;

		/// <summary>
		/// Writable engine directory. Uses the user's settings folder for installed builds.
		/// </summary>
		public static DirectoryReference WritableEngineDirectory
		{
			get
			{
				if (CachedWritableEngineDirectory == null)
				{
					DirectoryReference? UserDir = null;
					if (Unreal.IsEngineInstalled())
					{
						UserDir = Utils.GetUserSettingDirectory();
					}
					if (UserDir == null)
					{
						CachedWritableEngineDirectory = Unreal.EngineDirectory;
					}
					else
					{
						CachedWritableEngineDirectory = DirectoryReference.Combine(UserDir, "UnrealEngine");
					}
				}
				return CachedWritableEngineDirectory;
			}
		}

		/// <summary>
		/// The engine programs directory
		/// </summary>
		public static DirectoryReference EngineProgramSavedDirectory
		{
			get
			{
				if (CachedEngineProgramSavedDirectory == null)
				{
					if (Unreal.IsEngineInstalled())
					{
						CachedEngineProgramSavedDirectory = Utils.GetUserSettingDirectory() ?? DirectoryReference.Combine(Unreal.EngineDirectory, "Programs");
					}
					else
					{
						CachedEngineProgramSavedDirectory = DirectoryReference.Combine(Unreal.EngineDirectory, "Programs");
					}
				}
				return CachedEngineProgramSavedDirectory;
			}
		}

		// cached dictionary of BaseDir to extension directories
		private static Dictionary<DirectoryReference, Tuple<List<DirectoryReference>, List<DirectoryReference>>> CachedExtensionDirectories = new Dictionary<DirectoryReference, Tuple<List<DirectoryReference>, List<DirectoryReference>>>();

		/// <summary>
		/// Finds all the extension directories for the given base directory. This includes platform extensions and restricted folders.
		/// </summary>
		/// <param name="BaseDir">Location of the base directory</param>
		/// <param name="bIncludePlatformDirectories">If true, platform subdirectories are included (will return platform directories under Restricted dirs, even if bIncludeRestrictedDirectories is false)</param>
		/// <param name="bIncludeRestrictedDirectories">If true, restricted (NotForLicensees, NoRedist) subdirectories are included</param>
		/// <param name="bIncludeBaseDirectory">If true, BaseDir is included</param>
		/// <returns>List of extension directories, including the given base directory</returns>
		public static List<DirectoryReference> GetExtensionDirs(DirectoryReference BaseDir, bool bIncludePlatformDirectories=true, bool bIncludeRestrictedDirectories=true, bool bIncludeBaseDirectory=true)
		{
			Tuple<List<DirectoryReference>, List<DirectoryReference>>? CachedDirs;
			if (!CachedExtensionDirectories.TryGetValue(BaseDir, out CachedDirs))
			{
				CachedDirs = Tuple.Create(new List<DirectoryReference>(), new List<DirectoryReference>());

				CachedExtensionDirectories[BaseDir] = CachedDirs;

				DirectoryReference PlatformExtensionBaseDir = DirectoryReference.Combine(BaseDir, "Platforms");
				if (DirectoryReference.Exists(PlatformExtensionBaseDir))
				{
					CachedDirs.Item1.AddRange(DirectoryReference.EnumerateDirectories(PlatformExtensionBaseDir));
				}

				DirectoryReference RestrictedBaseDir = DirectoryReference.Combine(BaseDir, "Restricted");
				if (DirectoryReference.Exists(RestrictedBaseDir))
				{
					IEnumerable<DirectoryReference> RestrictedDirs = DirectoryReference.EnumerateDirectories(RestrictedBaseDir);
					CachedDirs.Item2.AddRange(RestrictedDirs);

					// also look for nested platforms in the restricted
					foreach (DirectoryReference RestrictedDir in RestrictedDirs)
					{
						DirectoryReference RestrictedPlatformExtensionBaseDir = DirectoryReference.Combine(RestrictedDir, "Platforms");
						if (DirectoryReference.Exists(RestrictedPlatformExtensionBaseDir))
						{
							CachedDirs.Item1.AddRange(DirectoryReference.EnumerateDirectories(RestrictedPlatformExtensionBaseDir));
						}
					}
				}

				// remove any platform directories in non-engine locations if the engine doesn't have the platform 
				if (BaseDir != Unreal.EngineDirectory && CachedDirs.Item1.Count > 0)
				{
					// if the DDPI.ini file doesn't exist, we haven't synced the platform, so just skip this directory
					CachedDirs.Item1.RemoveAll(x => DataDrivenPlatformInfo.GetDataDrivenInfoForPlatform(x.GetDirectoryName()) == null);
				}
			}

			// now return what the caller wanted (always include BaseDir)
			List<DirectoryReference> ExtensionDirs = new List<DirectoryReference>();
			if (bIncludeBaseDirectory)
			{
				ExtensionDirs.Add(BaseDir);
			}
			if (bIncludePlatformDirectories)
			{
				ExtensionDirs.AddRange(CachedDirs.Item1);
			}
			if (bIncludeRestrictedDirectories)
			{
				ExtensionDirs.AddRange(CachedDirs.Item2);
			}
			return ExtensionDirs;
		}

		/// <summary>
		/// Finds all the extension directories for the given base directory. This includes platform extensions and restricted folders.
		/// </summary>
		/// <param name="BaseDir">Location of the base directory</param>
		/// <param name="SubDir">The subdirectory to find</param>
		/// <param name="bIncludePlatformDirectories">If true, platform subdirectories are included (will return platform directories under Restricted dirs, even if bIncludeRestrictedDirectories is false)</param>
		/// <param name="bIncludeRestrictedDirectories">If true, restricted (NotForLicensees, NoRedist) subdirectories are included</param>
		/// <param name="bIncludeBaseDirectory">If true, BaseDir is included</param>
		/// <returns>List of extension directories, including the given base directory</returns>
		public static List<DirectoryReference> GetExtensionDirs(DirectoryReference BaseDir, string SubDir, bool bIncludePlatformDirectories=true, bool bIncludeRestrictedDirectories=true, bool bIncludeBaseDirectory=true)
		{
			return GetExtensionDirs(BaseDir, bIncludePlatformDirectories, bIncludeRestrictedDirectories, bIncludeBaseDirectory).Select(x => DirectoryReference.Combine(x, SubDir)).Where(x => DirectoryReference.Exists(x)).ToList();
		}

		/// <summary>
		/// The Remote Ini directory.  This should always be valid when compiling using a remote server.
		/// </summary>
		static string? RemoteIniPath = null;

		/// <summary>
		/// Returns true if UnrealBuildTool is running using an installed project (ie. a mod kit)
		/// </summary>
		/// <returns>True if running using an installed project</returns>
		static public bool IsProjectInstalled()
		{
			if (!bIsProjectInstalled.HasValue)
			{
				FileReference InstalledProjectLocationFile = FileReference.Combine(Unreal.RootDirectory, "Engine", "Build", "InstalledProjectBuild.txt");
				if (FileReference.Exists(InstalledProjectLocationFile))
				{
					InstalledProjectFile = FileReference.Combine(Unreal.RootDirectory, File.ReadAllText(InstalledProjectLocationFile.FullName).Trim());
					bIsProjectInstalled = true;
				}
				else
				{
					InstalledProjectFile = null;
					bIsProjectInstalled = false;
				}
			}
			return bIsProjectInstalled.Value;
		}

		/// <summary>
		/// Gets the installed project file
		/// </summary>
		/// <returns>Location of the installed project file</returns>
		static public FileReference? GetInstalledProjectFile()
		{
			if(IsProjectInstalled())
			{
				return InstalledProjectFile;
			}
			else
			{
				return null;
			}
		}

		/// <summary>
		/// Checks whether the given file is under an installed directory, and should not be overridden
		/// </summary>
		/// <param name="File">File to test</param>
		/// <returns>True if the file is part of the installed distribution, false otherwise</returns>
		static public bool IsFileInstalled(FileReference File)
		{
			if(Unreal.IsEngineInstalled() && File.IsUnderDirectory(Unreal.EngineDirectory))
			{
				return true;
			}
			if(IsProjectInstalled() && File.IsUnderDirectory(InstalledProjectFile!.Directory))
			{
				return true;
			}
			return false;
		}

		/// <summary>
		/// Gets the absolute path to the UBT assembly.
		/// </summary>
		/// <returns>A string containing the path to the UBT assembly.</returns>
		static public FileReference GetUBTPath()
		{
			return Unreal.UnrealBuildToolPath;
		}

		/// <summary>
		/// The Unreal remote tool ini directory.  This should be valid if compiling using a remote server
		/// </summary>
		/// <returns>The directory path</returns>
		static public string? GetRemoteIniPath()
		{
			return RemoteIniPath;
		}

		static public void SetRemoteIniPath(string Path)
		{
			RemoteIniPath = Path;
		}

		/// <summary>
		/// Global options for UBT (any modes)
		/// </summary>
		class GlobalOptions
		{
			/// <summary>
			/// User asked for help
			/// </summary>
			[CommandLine(Prefix = "-Help", Description = "Display this help.")]
			[CommandLine(Prefix = "-h")]
			[CommandLine(Prefix = "--help")]
			public bool bGetHelp = false;
			
			/// <summary>
			/// The amount of detail to write to the log
			/// </summary>
			[CommandLine(Prefix = "-Verbose", Value ="Verbose", Description = "Increase output verbosity")]
			[CommandLine(Prefix = "-VeryVerbose", Value ="VeryVerbose", Description = "Increase output verbosity more")]
			public LogEventType LogOutputLevel = LogEventType.Log;

			/// <summary>
			/// Specifies the path to a log file to write. Note that the default mode (eg. building, generating project files) will create a log file by default if this not specified.
			/// </summary>
			[CommandLine(Prefix = "-Log", Description = "Specify a log file location instead of the default Engine/Programs/UnrealBuildTool/Log.txt")]
			public FileReference? LogFileName = null;

			/// <summary>
			/// Whether to include timestamps in the log
			/// </summary>
			[CommandLine(Prefix = "-Timestamps", Description = "Include timestamps in the log")]
			public bool bLogTimestamps = false;

			/// <summary>
			/// Whether to format messages in MsBuild format
			/// </summary>
			[CommandLine(Prefix = "-FromMsBuild", Description = "Format messages for msbuild")]
			public bool bLogFromMsBuild = false;

			/// <summary>
			/// Whether to write progress markup in a format that can be parsed by other programs
			/// </summary>
			[CommandLine(Prefix = "-Progress", Description = "Write progress messages in a format that can be parsed by other programs")]
			public bool bWriteProgressMarkup = false;

			/// <summary>
			/// Whether to ignore the mutex
			/// </summary>
			[CommandLine(Prefix = "-NoMutex", Description = "Allow more than one instance of the program to run at once")]
			public bool bNoMutex = false;

			/// <summary>
			/// Whether to wait for the mutex rather than aborting immediately
			/// </summary>
			[CommandLine(Prefix = "-WaitMutex", Description = "Wait for another instance to finish and then start, rather than aborting immediately")]
			public bool bWaitMutex = false;

			/// <summary>
			/// </summary>
			[CommandLine(Prefix = "-RemoteIni", Description = "Remote tool ini directory")]
			public string RemoteIni = "";

			/// <summary>
			/// The mode to execute
			/// </summary>
			[CommandLine("-Mode=")] // description handling is special-cased in PrintUsage()

			[CommandLine("-Clean", Value="Clean", Description = "Clean build products. Equivalent to -Mode=Clean")]

			[CommandLine("-ProjectFiles", Value="GenerateProjectFiles", Description = "Generate project files based on IDE preference. Equivalent to -Mode=GenerateProjectFiles")]
			[CommandLine("-ProjectFileFormat=", Value="GenerateProjectFiles", Description = "Generate project files in specified format. May be used multiple times.")]
			[CommandLine("-Makefile", Value="GenerateProjectFiles", Description = "Generate Linux Makefile")]
			[CommandLine("-CMakefile", Value="GenerateProjectFiles", Description = "Generate project files for CMake")]
			[CommandLine("-QMakefile", Value="GenerateProjectFiles", Description = "Generate project files for QMake")]
			[CommandLine("-KDevelopfile", Value="GenerateProjectFiles", Description = "Generate project files for KDevelop")]
			[CommandLine("-CodeliteFiles", Value="GenerateProjectFiles", Description = "Generate project files for Codelite")]
			[CommandLine("-XCodeProjectFiles", Value="GenerateProjectFiles", Description = "Generate project files for XCode")]
			[CommandLine("-EddieProjectFiles", Value="GenerateProjectFiles", Description = "Generate project files for Eddie")]
			[CommandLine("-VSCode", Value="GenerateProjectFiles", Description = "Generate project files for Visual Studio Code")]
			[CommandLine("-VSMac", Value="GenerateProjectFiles", Description = "Generate project files for Visual Studio Mac")]
			[CommandLine("-CLion", Value="GenerateProjectFiles", Description = "Generate project files for CLion")]
			[CommandLine("-Rider", Value="GenerateProjectFiles", Description = "Generate project files for Rider")]
			#if __VPROJECT_AVAILABLE__
				[CommandLine("-VProject", Value = "GenerateProjectFiles")]
			#endif
			public string? Mode = null;

			/// <summary>
			/// Initialize the options with the given command line arguments
			/// </summary>
			/// <param name="Arguments"></param>
			public GlobalOptions(CommandLineArguments Arguments)
			{
				Arguments.ApplyTo(this);
				if (!string.IsNullOrEmpty(RemoteIni))
				{
					UnrealBuildTool.SetRemoteIniPath(RemoteIni);
				}
			}
		}

		/// <summary>
		/// Get all the valid Modes
		/// </summary>
		/// <returns></returns>
		private static Dictionary<string, Type> GetModes()
		{
			Dictionary<string, Type> ModeNameToType = new Dictionary<string, Type>(StringComparer.OrdinalIgnoreCase);
			foreach (Type Type in Assembly.GetExecutingAssembly().GetTypes())
			{
				if (Type.IsClass && !Type.IsAbstract && Type.IsSubclassOf(typeof(ToolMode)))
				{
					ToolModeAttribute? Attribute = Type.GetCustomAttribute<ToolModeAttribute>();
					if (Attribute == null)
					{
						throw new BuildException("Class '{0}' should have a ToolModeAttribute", Type.Name);
					}
					ModeNameToType.Add(Attribute.Name, Type);
				}
			}
			return ModeNameToType;
		}
		public static readonly Dictionary<string, Type> ModeNameToType = GetModes();

		/// <summary>
		/// Print (incomplete) usage information
		/// </summary>
		private static void PrintUsage()
		{
			Console.WriteLine("Global options:");
			int LongestPrefix = 0;
			foreach (FieldInfo Info in typeof(GlobalOptions).GetFields())
			{
				foreach (CommandLineAttribute Att in Info.GetCustomAttributes<CommandLineAttribute>())
				{
					if (Att.Prefix != null && Att.Description != null)
					{
						LongestPrefix = Att.Prefix.Length > LongestPrefix ? Att.Prefix.Length : LongestPrefix;
					}
				}
			}

			foreach (FieldInfo Info in typeof(GlobalOptions).GetFields())
			{
				foreach (CommandLineAttribute Att in Info.GetCustomAttributes<CommandLineAttribute>())
				{
					if (Att.Prefix != null && Att.Description != null)
					{
						Console.WriteLine($"  {Att.Prefix.PadRight(LongestPrefix)} :  {Att.Description}");
					}

					// special case for Mode
					if (String.Equals(Att.Prefix, "-Mode="))
					{
						Console.WriteLine($"  {Att.Prefix!.PadRight(LongestPrefix)} :  Select tool mode. One of the following (default tool mode is \"Build\"):");
						string Indent = "".PadRight(LongestPrefix + 8);
						string Line = Indent;
						IOrderedEnumerable<string> SortedModeNames = ModeNameToType.Keys.ToList().OrderBy(Name => Name);
						foreach (string ModeName in SortedModeNames.SkipLast(1))
						{
							Line += $"{ModeName}, ";
							if (Line.Length > 110)
							{
								Console.WriteLine(Line);
								Line = Indent;
							}
						}
						Line += SortedModeNames.Last();
						Console.WriteLine(Line);
					}
				}
			}
		}

		/// <summary>
		/// Main entry point. Parses any global options and initializes the logging system, then invokes the appropriate command.
		/// </summary>
		/// <param name="ArgumentsArray">Command line arguments</param>
		/// <returns>Zero on success, non-zero on error</returns>
		private static int Main(string[] ArgumentsArray)
		{
			SingleInstanceMutex? Mutex = null;
			try
			{
				// Start capturing performance info
				Timeline.Start();

				// Parse the command line arguments
				CommandLineArguments Arguments = new CommandLineArguments(ArgumentsArray);

				// Parse the global options
				GlobalOptions Options = new GlobalOptions(Arguments);

				Console.WriteLine(Arguments.CountValueArguments());

				if (
					// Print usage if there are zero arguments provided
					ArgumentsArray.Length == 0 

					// Print usage if the user asks for help
					|| Options.bGetHelp 

					// Print usage if none of the arguments provided were recognized as part of GlobalOptions and there fewer than three value arguments:
					// For the default Build mode, at least three value arguments are required: target, platform, and configuration
					// This is an imperfect test of a valid command line - the Build ToolMode should provide further feedback if the command line is not valid
					|| (!Arguments.AreAnyArgumentsUsed() && Arguments.CountValueArguments() < 3)
					)
				{
					PrintUsage();
					return Options.bGetHelp ? 0 : 1;
				}
				
				// Configure the log system
				Log.OutputLevel = Options.LogOutputLevel;
				Log.IncludeTimestamps = Options.bLogTimestamps;
				Log.IncludeProgramNameWithSeverityPrefix = Options.bLogFromMsBuild;

				// Always start capturing logs as early as possible to later copy to a log file if the ToolMode desires it (we have to start capturing before we get the ToolModeOptions below)
				StartupTraceListener StartupTrace = new StartupTraceListener();
				Log.AddTraceListener(StartupTrace);

				// Configure the progress writer
				ProgressWriter.bWriteMarkup = Options.bWriteProgressMarkup;

				// Add the log writer if requested. When building a target, we'll create the writer for the default log file later.
				if(Options.LogFileName != null)
				{
					Log.AddFileWriter("LogTraceListener", Options.LogFileName);
				}

				// Ensure we can resolve any external assemblies that are not in the same folder as our assembly.
				AssemblyUtils.InstallAssemblyResolver(Path.GetDirectoryName(Assembly.GetEntryAssembly().GetOriginalLocation()));

				// Change the working directory to be the Engine/Source folder. We are likely running from Engine/Binaries/DotNET
				// This is critical to be done early so any code that relies on the current directory being Engine/Source will work.
				DirectoryReference.SetCurrentDirectory(UnrealBuildTool.EngineSourceDirectory);

				// Register encodings from Net FW as this is required when using Ionic as we do in multiple toolchains
				Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

				// Get the type of the mode to execute, using a fast-path for the build mode.
				Type? ModeType = typeof(BuildMode);
				if(Options.Mode != null)
				{
					// Try to get the correct mode
					if(!ModeNameToType.TryGetValue(Options.Mode, out ModeType))
					{
						Log.TraceError("No mode named '{0}'. Available modes are:\n  {1}", Options.Mode, String.Join("\n  ", ModeNameToType.Keys));
						return 1;
					}
				}

				// Get the options for which systems have to be initialized for this mode
				ToolModeOptions ModeOptions = ModeType.GetCustomAttribute<ToolModeAttribute>()!.Options;

				// if we don't care about the trace listener, toss it now
				if ((ModeOptions & ToolModeOptions.UseStartupTraceListener) == 0)
				{
					Log.RemoveTraceListener(StartupTrace);
				}

				// Start prefetching the contents of the engine folder
				if((ModeOptions & ToolModeOptions.StartPrefetchingEngine) != 0)
				{
					using(Timeline.ScopeEvent("FileMetadataPrefetch.QueueEngineDirectory()"))
					{
						FileMetadataPrefetch.QueueEngineDirectory();
					}
				}

				// Read the XML configuration files
				if((ModeOptions & ToolModeOptions.XmlConfig) != 0)
				{
					using(Timeline.ScopeEvent("XmlConfig.ReadConfigFiles()"))
					{
						string XmlConfigMutexName = SingleInstanceMutex.GetUniqueMutexForPath("UnrealBuildTool_Mutex_XmlConfig", Assembly.GetExecutingAssembly().CodeBase!);
						using(SingleInstanceMutex XmlConfigMutex = new SingleInstanceMutex(XmlConfigMutexName, true))
						{
							FileReference? XmlConfigCache = Arguments.GetFileReferenceOrDefault("-XmlConfigCache=", null);
							XmlConfig.ReadConfigFiles(XmlConfigCache);
						}
					}
				}

				// Acquire a lock for this branch
				if((ModeOptions & ToolModeOptions.SingleInstance) != 0 && !Options.bNoMutex)
				{
					using(Timeline.ScopeEvent("SingleInstanceMutex.Acquire()"))
					{
						string MutexName = SingleInstanceMutex.GetUniqueMutexForPath("UnrealBuildTool_Mutex", Assembly.GetExecutingAssembly().CodeBase!);
						Mutex = new SingleInstanceMutex(MutexName, Options.bWaitMutex);
					}
				}

				// Register all the build platforms
				if((ModeOptions & ToolModeOptions.BuildPlatforms) != 0)
				{
					using(Timeline.ScopeEvent("UEBuildPlatform.RegisterPlatforms()"))
					{
						UEBuildPlatform.RegisterPlatforms(false, false);
					}
				}
				if ((ModeOptions & ToolModeOptions.BuildPlatformsHostOnly) != 0)
				{
					using (Timeline.ScopeEvent("UEBuildPlatform.RegisterPlatforms()"))
					{
						UEBuildPlatform.RegisterPlatforms(false, true);
					}
				}
				if ((ModeOptions & ToolModeOptions.BuildPlatformsForValidation) != 0)
				{
					using(Timeline.ScopeEvent("UEBuildPlatform.RegisterPlatforms()"))
					{
						UEBuildPlatform.RegisterPlatforms(true, false);
					}
				}

				// Create the appropriate handler
				ToolMode Mode = (ToolMode)Activator.CreateInstance(ModeType)!;

				// Execute the mode
				int Result = Mode.Execute(Arguments);
				if((ModeOptions & ToolModeOptions.ShowExecutionTime) != 0)
				{
					Log.TraceInformation("Total execution time: {0:0.00} seconds", Timeline.Elapsed.TotalSeconds);
				}
				return Result;
			}
			catch (CompilationResultException Ex)
			{
				// Used to return a propagate a specific exit code after an error has occurred. Does not log any message.
				Log.TraceLog(ExceptionUtils.FormatExceptionDetails(Ex));
				return (int)Ex.Result;
			}
			catch (BuildException Ex)
			{
				// BuildExceptions should have nicely formatted messages. We can log these directly.
				Log.TraceError(ExceptionUtils.FormatException(Ex));
				Log.TraceLog(ExceptionUtils.FormatExceptionDetails(Ex));
				return (int)CompilationResult.OtherCompilationError;
			}
			catch (Exception Ex)
			{
				// Unhandled exception.
				Log.TraceError("Unhandled exception: {0}", ExceptionUtils.FormatException(Ex));
				Log.TraceLog(ExceptionUtils.FormatExceptionDetails(Ex));
				return (int)CompilationResult.OtherCompilationError;
			}
			finally
			{
				// Cancel the prefetcher
				using(Timeline.ScopeEvent("FileMetadataPrefetch.Stop()"))
				{
					FileMetadataPrefetch.Stop();
				}

				// Print out all the performance info
				Timeline.Print(TimeSpan.FromMilliseconds(20.0), LogEventType.Log);

				// Make sure we flush the logs however we exit
				Trace.Close();

				// Write any trace logs
				TraceSpan.Flush();

				// Dispose of the mutex. Must be done last to ensure that another process does not startup and start trying to write to the same log file.
				if (Mutex != null)
				{
					Mutex.Dispose();
				}
			}
		}
	}
}

