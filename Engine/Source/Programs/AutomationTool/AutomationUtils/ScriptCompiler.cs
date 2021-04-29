// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using System.Reflection;
using System.Diagnostics;
using UnrealBuildTool;
using EpicGames.Core;
using System.Threading.Tasks;

namespace AutomationTool
{
	/// <summary>
	/// Exception thrown by PreprocessScriptFile.
	/// </summary>
	public class CompilationException : AutomationException
	{
		public CompilationException(string Filename, int StartLine, int StartColumn, int EndLine, int EndColumn, string Message, params string[] Args)
			: base(String.Format("Compilation Failed.\n>{0}({1},{2},{3},{4}): error: {5}", Path.GetFullPath(Filename), StartLine + 1, StartColumn + 1, EndLine + 1, EndColumn + 1,
			String.Format(Message, Args)))
		{
		}
	}

	/// <summary>
	/// Compiles and loads script assemblies.
	/// </summary>
	public static class ScriptCompiler
	{
		private static Dictionary<string, Type> ScriptCommands;
		private static HashSet<Assembly> AllCompiledAssemblies;
#if DEBUG
		const string BuildConfig = "Debug";
#else
		const string BuildConfig = "Development";
#endif
		const string DefaultScriptsDLLName = "AutomationScripts.Automation.dll";

		/// <summary>
		/// Finds and/or compiles all script files and assemblies.
		/// </summary>
		/// <param name="ScriptsForProjectFileName">Path to the current project. May be null, in which case we compile scripts for all projects.</param>
		/// <param name="AdditionalScriptsFolders">Additional script fodlers to look for source files in.</param>
		public static void FindAndCompileAllScripts(string ScriptsForProjectFileName, List<string> AdditionalScriptsFolders)
		{
			// Find all the project files
			Stopwatch SearchTimer = Stopwatch.StartNew();
			List<FileReference> ProjectFiles = FindAutomationProjects(ScriptsForProjectFileName, AdditionalScriptsFolders);
			Log.TraceLog("Found {0} project files in {1:0.000}s", ProjectFiles.Count, SearchTimer.Elapsed.TotalSeconds);
			foreach(FileReference ProjectFile in ProjectFiles)
			{
				Log.TraceLog("  {0}", ProjectFile);
			}

			// Get the default properties for compiling the projects
			Dictionary<string, string> MsBuildProperties = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			MsBuildProperties.Add("Platform", "AnyCPU");
			MsBuildProperties.Add("Configuration", BuildConfig);
			MsBuildProperties.Add("EngineDir", CommandUtils.EngineDirectory.FullName);

			// Read all the projects
			Stopwatch ParsingTimer = Stopwatch.StartNew();
			CsProjectInfo[] Projects = new CsProjectInfo[ProjectFiles.Count];
			Parallel.For(0, ProjectFiles.Count, Idx => Projects[Idx] = CsProjectInfo.Read(ProjectFiles[Idx], MsBuildProperties));
			Log.TraceLog("Parsed project files in {0:0.000}s", ParsingTimer.Elapsed.TotalSeconds);

			// Get all the build artifacts
			BuildProducts = new HashSet<FileReference>();

			HashSet<DirectoryReference> OutputDirs = new HashSet<DirectoryReference>();
			OutputDirs.Add(DirectoryReference.Combine(CommandUtils.EngineDirectory, "Binaries", "DotNET")); // Don't want any artifacts from this directory (just AutomationTool.exe and AutomationScripts.dll)

			foreach (CsProjectInfo Project in Projects)
			{
				DirectoryReference OutputDir;
				if (!Project.TryGetOutputDir(out OutputDir))
				{
					throw new AutomationException("Unable to get output directory for {0}", Project.ProjectPath);
				}

				if (OutputDirs.Add(OutputDir))
				{
					if (DirectoryReference.Exists(OutputDir))
					{
						BuildProducts.UnionWith(DirectoryReference.EnumerateFiles(OutputDir));
					}
					else
					{
						Log.TraceLog("Output directory {0} does not exist; ignoring", OutputDir);
					}
				}
			}

			// Load everything
			Stopwatch LoadTimer = Stopwatch.StartNew();
			List<Assembly> Assemblies = LoadAutomationAssemblies(Projects);
			AllCompiledAssemblies = new HashSet<Assembly>(Assemblies);
			Log.TraceLog("Loaded assemblies in {0:0.000}s", LoadTimer.Elapsed.TotalSeconds);

			// Setup platforms
			Platform.InitializePlatforms(Assemblies.ToArray());

			// Instantiate all the automation classes for interrogation
			Log.TraceVerbose("Creating commands.");
			ScriptCommands = new Dictionary<string, Type>(StringComparer.InvariantCultureIgnoreCase);
			foreach (Assembly CompiledScripts in Assemblies)
			{
				try
				{
					foreach (Type ClassType in CompiledScripts.GetTypes())
					{
						if (ClassType.IsSubclassOf(typeof(BuildCommand)) && ClassType.IsAbstract == false)
						{
							if (ScriptCommands.ContainsKey(ClassType.Name) == false)
							{
								ScriptCommands.Add(ClassType.Name, ClassType);
							}
							else
							{
								bool IsSame = string.Equals(ClassType.AssemblyQualifiedName, ScriptCommands[ClassType.Name].AssemblyQualifiedName);

								if (IsSame == false)
								{
									Log.TraceWarning("Unable to add command {0} twice. Previous: {1}, Current: {2}", ClassType.Name,
										ClassType.AssemblyQualifiedName, ScriptCommands[ClassType.Name].AssemblyQualifiedName);
								}
							}
						}
					}
				}
				catch (ReflectionTypeLoadException LoadEx)
				{
					foreach (Exception SubEx in LoadEx.LoaderExceptions)
					{
						Log.TraceWarning("Got type loader exception: {0}", SubEx.ToString());
					}
					throw new AutomationException("Failed to add commands from {0}. {1}", CompiledScripts, LoadEx);
				}
				catch (Exception Ex)
				{
					throw new AutomationException("Failed to add commands from {0}. {1}", CompiledScripts, Ex);
				}
			}
		}

		private static List<FileReference> FindAutomationProjects(string ScriptsForProjectFileName, List<string> AdditionalScriptsFolders)
		{
			// Configure the rules compiler
			// Get all game folders and convert them to build subfolders.
			List<DirectoryReference> AllGameFolders = new List<DirectoryReference>();

			if (ScriptsForProjectFileName == null)
			{
				AllGameFolders = NativeProjects.EnumerateProjectFiles().Select(x => x.Directory).ToList();
			}
			else
			{
				// Project automation scripts currently require source engine builds
				if (!CommandUtils.IsEngineInstalled())
				{
					AllGameFolders = new List<DirectoryReference> { new DirectoryReference(Path.GetDirectoryName(ScriptsForProjectFileName)) };
				}
			}

			List<DirectoryReference> AllAdditionalScriptFolders = AdditionalScriptsFolders.Select(x => new DirectoryReference(x)).ToList();
			foreach (DirectoryReference Folder in AllGameFolders)
			{
				DirectoryReference GameBuildFolder = DirectoryReference.Combine(Folder, "Build");
				if (DirectoryReference.Exists(GameBuildFolder))
				{
					AllAdditionalScriptFolders.Add(GameBuildFolder);
				}
			}

			Log.TraceVerbose("Discovering game folders.");

			List<FileReference> DiscoveredModules = UnrealBuildTool.RulesCompiler.FindAllRulesSourceFiles(UnrealBuildTool.RulesCompiler.RulesFileType.AutomationModule, GameFolders: AllGameFolders, ForeignPlugins: null, AdditionalSearchPaths: AllAdditionalScriptFolders);
			List<FileReference> ModulesToCompile = new List<FileReference>(DiscoveredModules.Count);
			foreach (FileReference ModuleFilename in DiscoveredModules)
			{
				if (HostPlatform.Current.IsScriptModuleSupported(ModuleFilename.GetFileNameWithoutAnyExtensions()))
				{
					ModulesToCompile.Add(ModuleFilename);
				}
				else
				{
					CommandUtils.LogVerbose("Script module {0} filtered by the Host Platform and will not be compiled.", ModuleFilename);
				}
			}

			return ModulesToCompile;
		}

		/// <summary>
		/// Loads all precompiled assemblies (DLLs that end with *Scripts.dll).
		/// </summary>
		/// <param name="Projects">Projects to load</param>
		/// <returns>List of compiled assemblies</returns>
		private static List<Assembly> LoadAutomationAssemblies(IEnumerable<CsProjectInfo> Projects)
		{
			List<Assembly> Assemblies = new List<Assembly>();
			foreach (CsProjectInfo Project in Projects)
			{
				// Get the output assembly name
				FileReference AssemblyLocation;
				if (!Project.TryGetOutputFile(out AssemblyLocation))
				{
					throw new AutomationException("Unable to get output file for {0}", Project.ProjectPath);
				}

				// Load the assembly into our app domain
				CommandUtils.LogLog("Loading script DLL: {0}", AssemblyLocation);
				try
				{
					AssemblyUtils.AddFileToAssemblyCache(AssemblyLocation.FullName);
					// Add a resolver for the Assembly directory, so that its dependencies may be found alongside it
					AssemblyUtils.InstallRecursiveAssemblyResolver(AssemblyLocation.Directory.FullName);
					Assembly Assembly = AppDomain.CurrentDomain.Load(AssemblyName.GetAssemblyName(AssemblyLocation.FullName));
					Assemblies.Add(Assembly);
				}
				catch (Exception Ex)
				{
					throw new AutomationException("Failed to load script DLL: {0}: {1}", AssemblyLocation, Ex.Message);
				}
			}
			return Assemblies;
		}

		public static HashSet<Assembly> GetCompiledAssemblies()
		{
			if (AllCompiledAssemblies == null)
			{
				return new HashSet<Assembly>();
			}
			else
			{
				return AllCompiledAssemblies;
			}
		}

		public static Dictionary<string, Type> Commands
		{
			get { return ScriptCommands; }
		}

		public static HashSet<FileReference> BuildProducts
		{
			get;
			private set;
		}
	}
}
