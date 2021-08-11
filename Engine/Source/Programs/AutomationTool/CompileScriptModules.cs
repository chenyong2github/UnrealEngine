// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Build.Execution;
using Microsoft.Build.Evaluation;
using Microsoft.Build.Framework;
using Microsoft.Build.Graph;
using Microsoft.Build.Locator;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Mail;
using System.Text;
using System.Text.Json;
using Microsoft.Build.Globbing;
using Microsoft.Build.Shared;
using UnrealBuildBase;

namespace AutomationToolDriver
{
    public partial class Program
    {
	    // Cache records of last-modified times for files
	    class WriteTimeCache
	    {
		    private Dictionary<string, DateTime> WriteTimes = new Dictionary<string, DateTime>();
			public DateTime GetLastWriteTime(string BasePath, string RelativeFilePath)
			{
				string NormalizedPath = Path.GetFullPath(RelativeFilePath, BasePath);
				if (!WriteTimes.TryGetValue(NormalizedPath, out DateTime WriteTime))
				{
					WriteTimes.Add(NormalizedPath, WriteTime = File.GetLastWriteTime(NormalizedPath));
				}
				return WriteTime;
			}
	    }
	    
		/// <summary>
	    /// Locates script modules, builds them if necessary, returns set of .dll files
		/// </summary>
		/// <param name="ScriptsForProjectFileName"></param>
		/// <param name="AdditionalScriptsFolders"></param>
		/// <param name="bForceCompile"></param>
		/// <param name="bUseBuildRecords"></param>
		/// <param name="bBuildSuccess"></param>
		/// <returns></returns>
		public static HashSet<FileReference> InitializeScriptModules(string ScriptsForProjectFileName, List<string> AdditionalScriptsFolders, bool bForceCompile, bool bUseBuildRecords, out bool bBuildSuccess)
		{
			WriteTimeCache WriteTimeCache = new WriteTimeCache();
			
			HashSet<FileReference> FoundAutomationProjects = FindAutomationProjects(ScriptsForProjectFileName, AdditionalScriptsFolders);
			
			if (!bForceCompile && bUseBuildRecords)
			{
				// fastest path: if we have an up-to-date record of a previous build, we should be able to start faster
				HashSet<FileReference> ScriptModules = TryGetAllUpToDateScriptModules(FoundAutomationProjects, ref WriteTimeCache);

				if (ScriptModules != null)
				{
					bBuildSuccess = true;
					return ScriptModules;
				}
			}

			// If the engine is installed, let's assume that there are valid .uatbuildrecord files (they live alongside .csproj files)
			// Which means if we didn't find them, there's a bigger problem that needs to be addressed)
			if (Unreal.IsEngineInstalled())
            {
				bBuildSuccess = false;
				return null;
            }

			// Fall back to the slower approach: use msbuild to load csproj files & build as necessary
			RegisterMsBuildPath();
			return BuildAllScriptPlugins(FoundAutomationProjects, bForceCompile, out bBuildSuccess, ref WriteTimeCache);
		}

		// Acceleration structure:
		// used to encapsulate a full set of dependencies for an msbuild project - explicit and (not yet) glob
		// .uatbuildrecord files are written to disk beside .csproj files (for ease of discovery)
		class UATBuildRecord
		{
			// Version number making it possible to quickly invalidate written records.
			public static readonly int CurrentVersion = 3;
			public int Version { get; set; } // what value does this get if deserialized from a file with no value for this field? 
			
			// The time that the target assembly was built (read from the file after the build)
			public DateTime TargetBuildTime { get; set; }
			
			// all paths are relative to the project directory
			
			// assembly (dll) location
			public string TargetPath { get; set; }
			
			// Paths of referenced projects
			public HashSet<string> ProjectReferences { get; set; } = new HashSet<string>();
			
			// file dependencies from non-glob sources
			public HashSet<string> Dependencies { get; set; } = new HashSet<string>();
			
			// file dependencies from globs
			public HashSet<string> GlobbedDependencies { get; set; } = new HashSet<string>();
			
			public class Glob
			{
				public string ItemType { get; set; }
				public List<string> Include { get; set; }
				public List<string> Exclude { get; set; }
				public List<string> Remove { get; set; }
			}

			public List<Glob> Globs { get; set; } = new List<Glob>();
		}

		private static bool ValidateGlobbedFiles(string ProjectDirectory, 
			List<UATBuildRecord.Glob> Globs, HashSet<string> GlobbedDependencies, out string ValidationFailureMessage)
		{
			// First, evaluate globs
			
			// Files are grouped by ItemType (e.g. Compile, EmbeddedResource) to ensure that Exclude and
			// Remove act as expected.
			Dictionary<string, HashSet<string>> Files = new Dictionary<string, HashSet<string>>();
			foreach (UATBuildRecord.Glob Glob in Globs)
			{
				HashSet<string> TypedFiles;
				if (!Files.TryGetValue(Glob.ItemType, out TypedFiles))
				{
					TypedFiles = new HashSet<string>();
					Files.Add(Glob.ItemType, TypedFiles);
				}
				
				foreach (string IncludePath in Glob.Include)
				{
					TypedFiles.UnionWith(FileMatcher.Default.GetFiles(ProjectDirectory, IncludePath, Glob.Exclude));
				}

				foreach (string Remove in Glob.Remove)
				{
					// FileMatcher.IsMatch() doesn't handle inconsistent path separators correctly - which is why globs
					// are normalized when they are added to UATBuildRecord
					TypedFiles.RemoveWhere(F => FileMatcher.IsMatch(F, Remove));
				}
			}

			// Then, validation that our evaluation matches what we're comparing against

			bool bValid = true;
			StringBuilder ValidationFailureText = new StringBuilder();
			
			// Look for extra files that were found
			foreach (HashSet<string> TypedFiles in Files.Values)
			{
				foreach (string File in TypedFiles)
				{
					if (!GlobbedDependencies.Contains(File))
					{
						ValidationFailureText.AppendLine($"Found additional file {File}");
						bValid = false;
					}
				}
			}
			
			// Look for files that are missing
			foreach (string File in GlobbedDependencies)
			{
				bool bFound = false;
				foreach (HashSet<string> TypedFiles in Files.Values)
				{
					if (TypedFiles.Contains(File))
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					ValidationFailureText.AppendLine($"Did not find {File}");
					bValid = false;
				}
			}

			ValidationFailureMessage = ValidationFailureText.ToString();
			return bValid;
		}

		private static bool ValidateBuildRecord(UATBuildRecord BuildRecord, string ProjectDirectory, out string ValidationFailureMessage, 
			ref WriteTimeCache Cache)
		{
			string TargetRelativePath =
				Path.GetRelativePath(Unreal.EngineDirectory.FullName, BuildRecord.TargetPath);

			if (BuildRecord.Version != UATBuildRecord.CurrentVersion)
			{
				ValidationFailureMessage =
					$"version does not match: build record has version {BuildRecord.Version}; current version is {UATBuildRecord.CurrentVersion}";
				return false;
			}

			DateTime TargetWriteTime = Cache.GetLastWriteTime(ProjectDirectory, BuildRecord.TargetPath);

			if (BuildRecord.TargetBuildTime != TargetWriteTime)
			{
				ValidationFailureMessage =
					$"recorded target build time ({BuildRecord.TargetBuildTime}) does not match {TargetRelativePath} ({TargetWriteTime})";
				return false;
			}

			foreach (string Dependency in BuildRecord.Dependencies)
			{
				if (Cache.GetLastWriteTime(ProjectDirectory, Dependency) > TargetWriteTime)
				{
					ValidationFailureMessage = $"{Dependency} is newer than {TargetRelativePath}";
					return false;
				}
			}

			if (!ValidateGlobbedFiles(ProjectDirectory, BuildRecord.Globs, BuildRecord.GlobbedDependencies,
				out ValidationFailureMessage))
			{
				return false;
			}

			foreach (string Dependency in BuildRecord.GlobbedDependencies)
			{
				if (Cache.GetLastWriteTime(ProjectDirectory, Dependency) > TargetWriteTime)
				{
					ValidationFailureMessage = $"{Dependency} is newer than {TargetRelativePath}";
					return false;
				}
			}

			return true;
		}

		private static UATBuildRecord LoadUpToDateBuildRecord(FileReference ProjectPath, ref WriteTimeCache Cache)
		{
			// .uatbuildrecord files are created adjacent to .csproj files
			FileReference BuildRecordPath = ProjectPath.ChangeExtension(".uatbuildrecord");
			
			string AutomationProjectRelativePath =
				Path.GetRelativePath(Unreal.EngineDirectory.FullName, ProjectPath.FullName);
			
			// If there is a missing build record, we stop checking here and fall back to the slower path
			if (!FileReference.Exists(BuildRecordPath))
			{
				Log.TraceLog($"[{AutomationProjectRelativePath}] has no build record");
				return null;
			}

			// filesystem errors or json parsing might result in an exception. If that happens, we fall back to the
			// slower path - .uatbuildrecord files will be re-generated, other filesystem errors may persist
			try
			{
				UATBuildRecord BuildRecord =
					JsonSerializer.Deserialize<UATBuildRecord>(FileReference.ReadAllText(BuildRecordPath));

				if (!Unreal.IsEngineInstalled())
				{
					if (!ValidateBuildRecord(BuildRecord, ProjectPath.Directory.FullName,
						out string ValidationFailureMessage, ref Cache))
					{
						Log.TraceLog($"[{AutomationProjectRelativePath}] {ValidationFailureMessage}");
						return null;
					}
				}
				
				return BuildRecord;
			}
			catch(Exception Ex)
			{
				// Any problems accessing files or parsing json, stop checking & fall back to build path
				Log.TraceLog($"[{AutomationProjectRelativePath}] Script modules are not up to date: {Ex.Message}"); // {Ex.StackTrace}");
				return null;
			}
		}
		
		// Loads build records for each project, if they exist, and then checks all recorded build dependencies to ensure
		// that nothing has changed since the last build.
		// This function is (currently?) all-or-nothing: either all projects are up-to-date, or none are.
		private static HashSet<FileReference> TryGetAllUpToDateScriptModules(HashSet<FileReference> FoundAutomationProjects, ref WriteTimeCache Cache)
		{
			Dictionary<FileReference, UATBuildRecord> BuildRecords = new Dictionary<FileReference, UATBuildRecord>();

			bool LoadProjects(FileReference ProjectPath, ref WriteTimeCache Cache)
			{
				if (BuildRecords.ContainsKey(ProjectPath))
				{
					return true;
				}
				UATBuildRecord BuildRecord = LoadUpToDateBuildRecord(ProjectPath, ref Cache);
				if (BuildRecord == null)
				{
					return false;
				}

				BuildRecords.Add(ProjectPath, BuildRecord);

				foreach (string ReferencedProjectPath in BuildRecord.ProjectReferences)
				{
					FileReference FullProjectPath = FileReference.FromString(Path.GetFullPath(ReferencedProjectPath, ProjectPath.Directory.FullName));
					if (!LoadProjects(FullProjectPath, ref Cache))
					{
						return false;
					}
				}

				return true;
			}

			HashSet<FileReference> FoundAssemblies = new HashSet<FileReference>();
			foreach (FileReference AutomationProject in FoundAutomationProjects)
			{
				if (!LoadProjects(AutomationProject, ref Cache))
                {
					return null;
                }
				FoundAssemblies.Add(FileReference.Combine(AutomationProject.Directory, BuildRecords[AutomationProject].TargetPath));
			}

			// it is possible that a referenced project has been rebuilt separately, that it is up to date, but that
			// its build time is newer than a project that references it. Check for that.
			foreach (KeyValuePair<FileReference, UATBuildRecord> Entry in BuildRecords)
            {
				foreach(string ReferencedProjectPath in Entry.Value.ProjectReferences)
                {
					FileReference FullReferencedProjectPath = FileReference.FromString(Path.GetFullPath(ReferencedProjectPath, Entry.Key.Directory.FullName));
					if (BuildRecords[FullReferencedProjectPath].TargetBuildTime > Entry.Value.TargetBuildTime)
                    {
						Log.TraceLog($"[{Entry.Key.MakeRelativeTo(Unreal.EngineDirectory)}] referenced project target {BuildRecords[FullReferencedProjectPath].TargetPath} build time ({BuildRecords[FullReferencedProjectPath].TargetBuildTime}) is more recent than this project's build time ({Entry.Value.TargetBuildTime})");
						return null;
                    }
                }
            }

			return FoundAssemblies;
		}
		
		/// <summary>
		/// Register our bundled dotnet installation to be used by Microsoft.Build
		/// This needs to happen in a function called before the first use of any Microsoft.Build types
		/// </summary>
		private static void RegisterMsBuildPath()
		{
			// Find our bundled dotnet SDK
			List<string> ListOfSdks = new List<string>();
			ProcessStartInfo StartInfo = new ProcessStartInfo
			{
				FileName = Unreal.DotnetPath.FullName,
				RedirectStandardOutput = true,
				UseShellExecute = false,
				ArgumentList = { "--list-sdks" }
			};
			StartInfo.EnvironmentVariables["DOTNET_MULTILEVEL_LOOKUP"] = "0"; // use only the bundled dotnet installation - ignore any other/system dotnet install

			Process DotnetProcess = Process.Start(StartInfo);
			{
				string Line;
				while ((Line = DotnetProcess.StandardOutput.ReadLine()) != null)
				{
					ListOfSdks.Add(Line);
				}
			}
			DotnetProcess.WaitForExit();

			if (ListOfSdks.Count != 1)
			{
				throw new Exception("Expected only one sdk installed for bundled dotnet");
			}

			// Expected output has this form:
			// 3.1.403 [D:\UE5_Main\engine\binaries\ThirdParty\DotNet\Windows\sdk]
			string SdkVersion = ListOfSdks[0].Split(' ')[0];

			DirectoryReference DotnetSdkDirectory = DirectoryReference.Combine(Unreal.DotnetDirectory, "sdk", SdkVersion);
			if (!DirectoryReference.Exists(DotnetSdkDirectory))
			{
				throw new Exception("Failed to find .NET SDK directory: " + DotnetSdkDirectory.FullName);
			}

			MSBuildLocator.RegisterMSBuildPath(DotnetSdkDirectory.FullName);
		}

		/// <summary>
		/// Determines if a particular script module is supported on the current platform;
		/// </summary>
		private static bool IsScriptModuleSupported(string ModuleName)
        {
			if (RuntimePlatform.IsMac)
            {
				List<string> UnsupportedModules = new List<string>()
				{
					"GauntletExtras", "GDK", "WinGDK", "XboxCommonGDK", "XboxOneGDK", "XSX",
					"FortniteGame", "PS4", "PS5", "Switch",
				};
				foreach (string UnsupportedModule in UnsupportedModules)
				{
					if (ModuleName.StartsWith(UnsupportedModule, StringComparison.OrdinalIgnoreCase))
					{
						return false;
					}
				}
            }
			else if (RuntimePlatform.IsLinux)
            {
				if (ModuleName.StartsWith("Gauntlet", StringComparison.OrdinalIgnoreCase))
				{
					return false;
				}
            }

			return true;
        }
		
		static HashSet<FileReference> FindAutomationProjects(string ScriptsForProjectFileName, List<string> AdditionalScriptsFolders)
		{
			HashSet<FileReference> FoundAutomationProjects = new HashSet<FileReference>();

			// Configure the rules compiler
			// Get all game folders and convert them to build subfolders.
			List<DirectoryReference> AllGameFolders = new List<DirectoryReference>();

			if (ScriptsForProjectFileName == null)
			{
				AllGameFolders = NativeProjectsBase.EnumerateProjectFiles().Select(x => x.Directory).ToList();
			}
			else
			{
				// Project automation scripts currently require source engine builds
				if (!Unreal.IsEngineInstalled())
				{
					DirectoryReference ScriptsDir = new DirectoryReference(Path.GetDirectoryName(ScriptsForProjectFileName));
					ScriptsDir = DirectoryReference.FindCorrectCase(ScriptsDir);
					AllGameFolders = new List<DirectoryReference> { ScriptsDir };
				}
			}

			AdditionalScriptsFolders = AdditionalScriptsFolders ?? new List<string>();
			List<DirectoryReference> AllAdditionalScriptFolders = AdditionalScriptsFolders.Select(
				x => DirectoryReference.FindCorrectCase(new DirectoryReference(x))).ToList();

			foreach (DirectoryReference Folder in AllGameFolders)
			{
				DirectoryReference GameBuildFolder = DirectoryReference.Combine(Folder, "Build");
				if (DirectoryReference.Exists(GameBuildFolder))
				{
					AllAdditionalScriptFolders.Add(GameBuildFolder);
				}
			}

			Log.TraceVerbose("Discovering game folders.");
			
			List<FileReference> DiscoveredModules = Rules.FindAllRulesSourceFiles(Rules.RulesFileType.AutomationModule,
				GameFolders: AllGameFolders, ForeignPlugins: null, AdditionalSearchPaths: AllAdditionalScriptFolders);
			foreach (FileReference ModuleFilename in DiscoveredModules)
			{
				if (IsScriptModuleSupported(ModuleFilename.GetFileNameWithoutAnyExtensions()))
				{
					FoundAutomationProjects.Add(ModuleFilename);
				}
				else
				{
					Log.TraceVerbose("Script module {0} filtered by the Host Platform and will not be compiled.", ModuleFilename);
				}
			}

			return FoundAutomationProjects;
		}

		class MLogger : ILogger
		{
			LoggerVerbosity ILogger.Verbosity { get => LoggerVerbosity.Normal; set => throw new NotImplementedException(); }
			string ILogger.Parameters { get => throw new NotImplementedException(); set { } }

			public bool bVeryVerboseLog = false;

			bool bFirstError = true;

			void ILogger.Initialize(IEventSource EventSource)
			{
				EventSource.ProjectStarted += new ProjectStartedEventHandler(eventSource_ProjectStarted);
				EventSource.TaskStarted += new TaskStartedEventHandler(eventSource_TaskStarted);
				EventSource.MessageRaised += new BuildMessageEventHandler(eventSource_MessageRaised);
				EventSource.WarningRaised += new BuildWarningEventHandler(eventSource_WarningRaised);
				EventSource.ErrorRaised += new BuildErrorEventHandler(eventSource_ErrorRaised);
				EventSource.ProjectFinished += new ProjectFinishedEventHandler(eventSource_ProjectFinished);
			}

			void eventSource_ErrorRaised(object Sender, BuildErrorEventArgs e)
			{
				if (bFirstError)
                {
					Trace.WriteLine("");
					Log.WriteLine(LogEventType.Console, "");
					bFirstError = false;
                }
				string Message = $"{e.File}({e.LineNumber},{e.ColumnNumber}): error {e.Code}: {e.Message} ({e.ProjectFile})";
				Trace.WriteLine(Message); // double-clickable message in VS output
				Log.WriteLine(LogEventType.Console, Message);
			}

			void eventSource_WarningRaised(object Sender, BuildWarningEventArgs e)
			{
				if (bFirstError)
                {
					Trace.WriteLine("");
					Log.WriteLine(LogEventType.Console, "");
					bFirstError = false;
                }
				string Message = $"{e.File}({e.LineNumber},{e.ColumnNumber}): warning {e.Code}: {e.Message} ({e.ProjectFile})";
				Trace.WriteLine(Message); // double-clickable message in VS output
				Log.WriteLine(LogEventType.Console, Message);
			}

			void eventSource_MessageRaised(object Sender, BuildMessageEventArgs e)
			{
				if (bVeryVerboseLog)
				{
					//if (!String.Equals(e.SenderName, "ResolveAssemblyReference"))
					//if (e.Message.Contains("atic"))
					{
						Log.WriteLine(LogEventType.Console, $"{e.SenderName}: {e.Message}");
					}
				}
			}

			void eventSource_ProjectStarted(object Sender, ProjectStartedEventArgs e)
			{
				if (bVeryVerboseLog)
				{
					Log.WriteLine(LogEventType.Console, $"{e.SenderName}: {e.Message}");
				}
			}

			void eventSource_ProjectFinished(object Sender, ProjectFinishedEventArgs e)
			{
				if (bVeryVerboseLog)
				{
					Log.WriteLine(LogEventType.Console, $"{e.SenderName}: {e.Message}");
				}
			}

			void eventSource_TaskStarted(object Sender, TaskStartedEventArgs e)
			{
				if (bVeryVerboseLog)
				{
					Log.WriteLine(LogEventType.Console, $"{e.SenderName}: {e.Message}");
				}
			}

			void ILogger.Shutdown()
			{
			}
		}

		static readonly Dictionary<string, string> GlobalProperties = new Dictionary<string, string>
            {
				{ "EngineDir", Unreal.EngineDirectory.FullName },
#if DEBUG
				{ "Configuration", "Debug" },
#else
				{ "Configuration", "Development" },
#endif
            };

		private static HashSet<FileReference> BuildAllScriptPlugins(HashSet<FileReference> FoundAutomationProjects, bool bForceCompile, out bool bBuildSuccess, ref WriteTimeCache Cache)
		{
			// The -IgnoreBuildRecords prevents the loading & parsing of .uatbuildrecord files - but UATBuildRecord objects will be used in this function regardless
			Dictionary<FileReference, UATBuildRecord> BuildRecords = new Dictionary<FileReference, UATBuildRecord>();
			
			{
				Dictionary<string, Project> Projects = new Dictionary<string, Project>();
				
				// Microsoft.Build.Evaluation.Project provides access to information stored in the .csproj xml that is 
				// not available when using Microsoft.Build.Execution.ProjectInstance (used later in this function and
				// in BuildProjects) - particularly, to access glob information defined in the source file.

				// Load all found automation projects, and any other referenced projects.
				foreach (FileReference ProjectPath in FoundAutomationProjects)
				{
					void LoadProjectAndReferences(string ProjectPath, string ReferencedBy)
					{
						ProjectPath = Path.GetFullPath(ProjectPath);
						if (!Projects.ContainsKey(ProjectPath))
						{
							Project Project;

							// Microsoft.Build.Evaluation.Project doesn't give a lot of useful information if this fails,
							// so make sure to print our own diagnostic info if something goes wrong
							try
							{
								Project = new Project(ProjectPath, GlobalProperties, toolsVersion: null);
							}
							catch (Microsoft.Build.Exceptions.InvalidProjectFileException IPFEx)
							{
								Log.TraceError($"Could not load project file {ProjectPath}");
								Log.TraceError(IPFEx.BaseMessage);
								
								if (!String.IsNullOrEmpty(ReferencedBy))
								{
									Log.TraceError($"Referenced by: {ReferencedBy}");
								}
								if (Projects.Count > 0)
								{
									Log.TraceError("See the log file for the list of previously loaded projects.");
									Log.TraceLog("Loaded projects (most recently loaded first):");
									foreach (string Path in Projects.Keys.Reverse())
                                    {
										Log.TraceLog($"  {Path}");
                                    }
								}
								throw IPFEx;
							}

							Projects.Add(ProjectPath, Project);
							ReferencedBy = String.IsNullOrEmpty(ReferencedBy) ? ProjectPath : $"{ProjectPath}{Environment.NewLine}{ReferencedBy}";
							foreach (string ReferencedProject in Project.GetItems("ProjectReference").
								Select(I => I.EvaluatedInclude))
							{
								LoadProjectAndReferences(Path.Combine(Project.DirectoryPath, ReferencedProject), ReferencedBy);
							}
						}
					}
					LoadProjectAndReferences(ProjectPath.FullName, null);
				}
				
				// generate a BuildRecord for each loaded project - the gathered information will be used to determine if the project is
				// out of date, and if building this project can be skipped. It is also used to populate the .uatbuildrecord after the
				// build completes
				foreach (Project Project in Projects.Values)
				{
					string TargetPath = Path.GetRelativePath(Project.DirectoryPath, Project.GetPropertyValue("TargetPath"));
					
					UATBuildRecord BuildRecord = new UATBuildRecord()
					{
						Version = UATBuildRecord.CurrentVersion,
						TargetPath = TargetPath,
						TargetBuildTime = Cache.GetLastWriteTime(Project.DirectoryPath, TargetPath),
					};

					// the .csproj
					BuildRecord.Dependencies.Add(Path.GetRelativePath(Project.DirectoryPath, Project.FullPath));
					
					// Imports: files included in the xml (typically props, targets, etc)
					foreach (ResolvedImport Import in Project.Imports)
					{
						string ImportPath = Path.GetRelativePath(Project.DirectoryPath, Import.ImportedProject.FullPath);
						
						// nuget.g.props and nuget.g.targets are generated by Restore, and are frequently re-written;
						// it should be safe to ignore these files - changes to references from a .csproj file will
						// show up as that file being out of date.
						if (ImportPath.IndexOf("nuget.g.") != -1)
						{
							continue;
						}

						BuildRecord.Dependencies.Add(ImportPath);
					}
					
					// References: e.g. Ionic.Zip.Reduced.dll, fastJSON.dll
					foreach (var Item in Project.GetItems("Reference"))
					{
						BuildRecord.Dependencies.Add(Item.GetMetadataValue("HintPath"));
					}

					foreach (ProjectItem ReferencedProjectItem in Project.GetItems("ProjectReference"))
					{
						BuildRecord.ProjectReferences.Add(ReferencedProjectItem.EvaluatedInclude);
					}

					foreach (ProjectItem CompileItem in Project.GetItems("Compile"))
					{
						if (FileMatcher.HasWildcards(CompileItem.UnevaluatedInclude))
						{
							BuildRecord.GlobbedDependencies.Add(CompileItem.EvaluatedInclude);
						}
						else
						{
							BuildRecord.Dependencies.Add(CompileItem.EvaluatedInclude);
						}
					}

					foreach (ProjectItem ContentItem in Project.GetItems("Content"))
                    {
						if (FileMatcher.HasWildcards(ContentItem.UnevaluatedInclude))
                        {
							BuildRecord.GlobbedDependencies.Add(ContentItem.EvaluatedInclude);
                        }
						else
                        {
							BuildRecord.Dependencies.Add(ContentItem.EvaluatedInclude);
                        }
                    }

					foreach (ProjectItem EmbeddedResourceItem in Project.GetItems("EmbeddedResource"))
					{
						if (FileMatcher.HasWildcards(EmbeddedResourceItem.UnevaluatedInclude))
						{
							BuildRecord.GlobbedDependencies.Add(EmbeddedResourceItem.EvaluatedInclude);
						}
						else
						{
							BuildRecord.Dependencies.Add(EmbeddedResourceItem.EvaluatedInclude);
						}
					}

					// this line right here is slow: ~30-40ms per project (which can be more than a second total)
					// making it one of the slowest steps in gathering or checking dependency information from
					// .csproj files (after loading as Microsoft.Build.Evalation.Project)
					// 
					// This also returns a lot more information than we care for - MSBuildGlob objects,
					// which have a range of precomputed values. It may be possible to take source for
					// GetAllGlobs() and construct a version that does less.
					var Globs = Project.GetAllGlobs();

					// FileMatcher.IsMatch() requires directory separators in glob strings to match the
					// local flavor. There's probably a better way.
					string CleanGlobString(string GlobString)
					{
						char Sep = Path.DirectorySeparatorChar;
						char NotSep = Sep == '/' ? '\\' : '/'; // AltDirectorySeparatorChar isn't always what we need (it's '/' on Mac)
					
						var Chars = GlobString.ToCharArray();
						int P = 0;
						for (int I = 0; I < GlobString.Length; ++I, ++P)
						{
							// Flip a non-native separator
							if (Chars[I] == NotSep)
							{
								Chars[P] = Sep;
							}
							else
							{
								Chars[P] = Chars[I];
							}

							// Collapse adjacent separators
							if (I > 0 && Chars[P] == Sep && Chars[P - 1] == Sep ) 
							{
								P -= 1;
							}
						}

						return new string(Chars, 0, P);
					}

					foreach (var Glob in Globs)
					{
						if (String.Equals("None", Glob.ItemElement.ItemType))
						{
							// don't record the default "None" glob - it's not (?) a trigger for any Automation rebuild
							continue;
						}

						List<string> Include = new List<string>(Glob.IncludeGlobs.Select(F => CleanGlobString(F)));
						List<string> Exclude = new List<string>(Glob.Excludes.Select(F => CleanGlobString(F)));
						List<string> Remove = new List<string>(Glob.Removes.Select(F => CleanGlobString(F)));
						
						BuildRecord.Globs.Add(new UATBuildRecord.Glob() { ItemType = Glob.ItemElement.ItemType, 
							Include = Include, Exclude = Exclude, Remove = Remove });
					}

					BuildRecords.Add(FileReference.FromString(Project.FullPath), BuildRecord);
				}
			}

			// Potential optimization: Contructing the ProjectGraph here gives the full graph of dependencies - which is nice,
			// but not strictly necessary, and slower than doing it some other way.
			ProjectGraph InputProjectGraph;
			InputProjectGraph = new ProjectGraph(FoundAutomationProjects.Select(P => P.FullName), GlobalProperties);

			// A ProjectGraph that will represent the set of projects that we actually want to build
			ProjectGraph BuildProjectGraph = null;

			if (bForceCompile)
			{
				Log.TraceLog("Script modules will build: '-Compile' on command line");
				BuildProjectGraph = InputProjectGraph;
			}
			else
			{
				HashSet<ProjectGraphNode> OutOfDateProjects = new HashSet<ProjectGraphNode>(FoundAutomationProjects.Count);

				foreach (ProjectGraphNode Project in InputProjectGraph.ProjectNodesTopologicallySorted)
				{
					UATBuildRecord BuildRecord = BuildRecords[FileReference.FromString(Project.ProjectInstance.FullPath)];

					string ValidationFailureMessage;
					if (!ValidateBuildRecord(BuildRecord, Project.ProjectInstance.Directory,
						out ValidationFailureMessage, ref Cache))
					{
						Log.TraceLog($"[{Path.GetFileName(Project.ProjectInstance.FullPath)}] is out of date:\n{ValidationFailureMessage}");
						OutOfDateProjects.Add(Project);
					}
				}

				// it is possible that a referenced project has been rebuilt separately, that it is up to date, but that
				// its build time is newer than a project that references it. Check for that.
				foreach (ProjectGraphNode Project in InputProjectGraph.ProjectNodesTopologicallySorted)
				{
					UATBuildRecord BuildRecord = BuildRecords[FileReference.FromString(Project.ProjectInstance.FullPath)];

					foreach(string ReferencedProjectPath in BuildRecord.ProjectReferences)
					{
						FileReference FullReferencedProjectPath = FileReference.FromString(Path.GetFullPath(ReferencedProjectPath, Project.ProjectInstance.Directory));
						if (BuildRecords[FullReferencedProjectPath].TargetBuildTime > BuildRecord.TargetBuildTime)
						{
							Log.TraceLog($"[{Path.GetFileName(Project.ProjectInstance.FullPath)}] referenced project target {BuildRecords[FullReferencedProjectPath].TargetPath} build time ({BuildRecords[FullReferencedProjectPath].TargetBuildTime}) is more recent than this project's build time ({BuildRecord.TargetBuildTime})");
							OutOfDateProjects.Add(Project);
						}
					}
				}

				// for any out of date project, mark everything that references it as out of date
				Queue<ProjectGraphNode> OutOfDateQueue = new Queue<ProjectGraphNode>(OutOfDateProjects);
				while (OutOfDateQueue.TryDequeue(out ProjectGraphNode OutOfDateProject))
				{
					foreach (ProjectGraphNode Referee in OutOfDateProject.ReferencingProjects)
					{
						if (OutOfDateProjects.Add(Referee))
						{
							OutOfDateQueue.Enqueue(Referee);
						}
					}
				}

				if (OutOfDateProjects.Count > 0)
				{
					BuildProjectGraph = new ProjectGraph(OutOfDateProjects.Select(P => P.ProjectInstance.FullPath), GlobalProperties);
				}
			}

			if (BuildProjectGraph != null)
			{
				bBuildSuccess = BuildProjects(BuildProjectGraph);
			}
			else
			{ 
				bBuildSuccess = true; 
			}

			// write all build records
			foreach (ProjectGraphNode ProjectNode in InputProjectGraph.ProjectNodes)
			{
				FileReference ProjectPath = FileReference.FromString(ProjectNode.ProjectInstance.FullPath);
				UATBuildRecord BuildRecord = BuildRecords[ProjectPath];
				
				// update target build times into build records to ensure everything is up-to-date
				FileReference FullPath = FileReference.Combine(ProjectPath.Directory, BuildRecord.TargetPath);
				BuildRecord.TargetBuildTime = FileReference.GetLastWriteTime(FullPath);

				File.WriteAllText( ProjectPath.ChangeExtension(".uatbuildrecord").FullName,
					JsonSerializer.Serialize(BuildRecords[ProjectPath], new JsonSerializerOptions {WriteIndented = true}));
			}

			// todo: re-verify build records after a build to verify that everything is actually up to date


			// even if only a subset was built, this function returns the full list of target assembly paths
			return new HashSet<FileReference>(InputProjectGraph.EntryPointNodes.Select(
				Project => FileReference.FromString(Project.ProjectInstance.GetPropertyValue("TargetPath"))));
        }

		private static bool BuildProjects(ProjectGraph AutomationProjectGraph)
		{
			var Logger = new MLogger();

			string[] TargetsToBuild = { "Restore", "Build" };

			bool Result = true;

			Log.TraceInformation($"Building {AutomationProjectGraph.EntryPointNodes.Count} projects (see Log for more details)");
			foreach (string TargetToBuild in TargetsToBuild)
			{
				var GraphRequest = new GraphBuildRequestData(AutomationProjectGraph, new string[] { TargetToBuild });

				var BuildMan = BuildManager.DefaultBuildManager;

				var BuildParameters = new BuildParameters();
				BuildParameters.AllowFailureWithoutError = false;
				BuildParameters.DetailedSummary = true;

				BuildParameters.Loggers = new List<ILogger> { Logger };
				BuildParameters.MaxNodeCount = 1; // msbuild bug - more than 1 here and the build stalls. Likely related to https://github.com/dotnet/msbuild/issues/1941

				BuildParameters.OnlyLogCriticalEvents = false;
				BuildParameters.ShutdownInProcNodeOnBuildFinish = false;

				BuildParameters.GlobalProperties = GlobalProperties;

				Log.TraceInformation($" {TargetToBuild}...");

				GraphBuildResult BuildResult = BuildMan.Build(BuildParameters, GraphRequest);

				if (BuildResult.OverallResult == BuildResultCode.Failure)
				{
					Log.TraceInformation("");
					foreach (var NodeResult in BuildResult.ResultsByNode)
					{
						if (NodeResult.Value.OverallResult == BuildResultCode.Failure)
						{
							Log.TraceError($"  Failed to build: {NodeResult.Key.ProjectInstance.FullPath}");
						}
					}
					Result = false;
				}
			}
			Log.TraceInformation(" build complete.");
			return Result;
		}
    }
}

