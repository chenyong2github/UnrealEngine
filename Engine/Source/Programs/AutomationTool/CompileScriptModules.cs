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
			public DateTime GetLastWriteTime(DirectoryReference BasePath, string RelativeFilePath)
			{
				string NormalizedPath = Path.GetFullPath(RelativeFilePath, BasePath.FullName);
				if (!WriteTimes.TryGetValue(NormalizedPath, out DateTime WriteTime))
				{
					WriteTimes.Add(NormalizedPath, WriteTime = File.GetLastWriteTime(NormalizedPath));
				}
				return WriteTime;
			}
	    }
	    
		static FileReference ConstructBuildRecordPath(FileReference ProjectPath, List<DirectoryReference> BaseDirectories)
		{
			DirectoryReference BasePath = null;

			foreach (DirectoryReference ScriptFolder in BaseDirectories)
			{
				if (ProjectPath.IsUnderDirectory(ScriptFolder))
				{
					BasePath = ScriptFolder;
					break;
				}
			}

			if (BasePath == null)
			{
				throw new Exception($"Unable to map csproj {ProjectPath} to Engine, game, or an additional script folder. Candidates were:{Environment.NewLine} {String.Join(Environment.NewLine, BaseDirectories)}");
			}

			DirectoryReference BuildRecordDirectory = DirectoryReference.Combine(BasePath!, "Intermediate", "ScriptModules");
			DirectoryReference.CreateDirectory(BuildRecordDirectory);

			return FileReference.Combine(BuildRecordDirectory, ProjectPath.GetFileName()).ChangeExtension(".json");
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
		public static HashSet<FileReference> InitializeScriptModules(string ScriptsForProjectFileName, List<string> AdditionalScriptsFolders, bool bForceCompile, bool bNoCompile, bool bUseBuildRecords, out bool bBuildSuccess)
		{
			WriteTimeCache WriteTimeCache = new WriteTimeCache();

			List<DirectoryReference> GameDirectories = GetGameDirectories(ScriptsForProjectFileName);
			List<DirectoryReference> AdditionalDirectories = GetAdditionalDirectories(AdditionalScriptsFolders);
			List<DirectoryReference> GameBuildDirectories = GetAdditionalBuildDirectories(GameDirectories);

			// List of directories used to locate Intermediate/ScriptModules dirs for writing build records
			List<DirectoryReference> BaseDirectories = new List<DirectoryReference>(1 + GameDirectories.Count + AdditionalDirectories.Count);
			BaseDirectories.Add(Unreal.EngineDirectory);
			BaseDirectories.AddRange(GameDirectories);
			BaseDirectories.AddRange(AdditionalDirectories);

			HashSet<FileReference> FoundAutomationProjects = new HashSet<FileReference>(
				Rules.FindAllRulesSourceFiles(Rules.RulesFileType.AutomationModule,
				// Project automation scripts require source engine builds
				GameFolders: Unreal.IsEngineInstalled() ? GameDirectories : new List<DirectoryReference>(), 
				ForeignPlugins: null, AdditionalSearchPaths: AdditionalDirectories.Concat(GameBuildDirectories).ToList()));

			bool bUseBuildRecordsOnlyForProjectDiscovery = bNoCompile || Unreal.IsEngineInstalled();

			// Load existing build records, validating them only if (re)compiling script projects is an option
			Dictionary<FileReference, (CsProjBuildRecord, FileReference)> ExistingBuildRecords = LoadExistingBuildRecords(BaseDirectories);

			Dictionary<FileReference, CsProjBuildRecord> ValidBuildRecords = new Dictionary<FileReference, CsProjBuildRecord>(ExistingBuildRecords.Count);
			Dictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath)> InvalidBuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>(ExistingBuildRecords.Count);

			if (bUseBuildRecords)
			{
				foreach (FileReference AutomationProject in FoundAutomationProjects)
				{
					ValidateBuildRecordRecursively(ValidBuildRecords, InvalidBuildRecords, ExistingBuildRecords, AutomationProject, ref WriteTimeCache);
				}
			}

			if (bUseBuildRecordsOnlyForProjectDiscovery)
            {
				// when the engine is installed, or UAT is invoked with -NoCompile, we expect to find at least one script module (AutomationUtils is a necessity)
				if (ExistingBuildRecords.Count == 0)
                {
					throw new Exception("Found no script module records.");
                }

				HashSet<FileReference> BuiltTargets = new HashSet<FileReference>(ExistingBuildRecords.Count);
				foreach ((CsProjBuildRecord BuildRecord, FileReference BuildRecordPath) in ExistingBuildRecords.Values.Where(x => x.Item2.HasExtension(".Automation.json")))
                {
					FileReference ProjectPath = FileReference.Combine(BuildRecordPath.Directory, BuildRecord.ProjectPath);
					FileReference TargetPath = FileReference.Combine(ProjectPath.Directory, BuildRecord.TargetPath);

					if (FileReference.Exists(TargetPath))
                    {
						BuiltTargets.Add(TargetPath);
                    }
                    else
                    {
						if (bNoCompile)
						{
							// when -NoCompile is on the command line, try to run with whatever is available
							Log.TraceWarning($"Script module \"{TargetPath}\" not found for record \"{BuildRecordPath}\"");
						}
						else
						{
							// when the engine is installed, expect to find a built target assembly for every record that was found
							throw new Exception($"Script module \"{TargetPath}\" not found for record \"{BuildRecordPath}\"");
						}
						
                    }
                }
				bBuildSuccess = true;
				return BuiltTargets;
            }
			else
            {
				// when the engine is not installed, delete any build record .json file that is not valid
				foreach ((CsProjBuildRecord _, FileReference BuildRecordPath) in InvalidBuildRecords.Values)
				{
					Log.TraceLog($"Deleting invalid build record \"{BuildRecordPath}\"");
					FileReference.Delete(BuildRecordPath);
                }
            }

			if (!bForceCompile && bUseBuildRecords)
			{
				// If all found records are valid, we can return their targets directly
				if (ValidBuildRecords.Count == FoundAutomationProjects.Count)
                {
					bBuildSuccess = true;
					return new HashSet<FileReference>(ValidBuildRecords.Select(x => FileReference.Combine(x.Key.Directory, x.Value.TargetPath)));
                }
			}

			// Fall back to the slower approach: use msbuild to load csproj files & build as necessary
			RegisterMsBuildPath();
			return BuildAllScriptPlugins(FoundAutomationProjects, bForceCompile || !bUseBuildRecords, out bBuildSuccess, ref WriteTimeCache, BaseDirectories);
		}

		/// <summary>
		/// Find and load existing build record .json files from any Intermediate/ScriptModules found in the provided lists
		/// </summary>
		/// <param name="BaseDirectories"></param>
		/// <returns></returns>
		static Dictionary<FileReference, (CsProjBuildRecord, FileReference)> LoadExistingBuildRecords(List<DirectoryReference> BaseDirectories)
        {
			Dictionary<FileReference, (CsProjBuildRecord, FileReference)> LoadedBuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>();

			foreach (DirectoryReference Directory in BaseDirectories)
			{
				DirectoryReference IntermediateDirectory = DirectoryReference.Combine(Directory, "Intermediate", "ScriptModules");
				if (!DirectoryReference.Exists(IntermediateDirectory))
				{
					continue;
				}

				foreach (FileReference JsonFile in DirectoryReference.EnumerateFiles(IntermediateDirectory, "*.json"))
                {
					CsProjBuildRecord BuildRecord = default;

					// filesystem errors or json parsing might result in an exception. If that happens, we fall back to the
					// slower path - if compiling, buildrecord files will be re-generated; other filesystem errors may persist
					try
					{
						BuildRecord = JsonSerializer.Deserialize<CsProjBuildRecord>(FileReference.ReadAllText(JsonFile));
						Log.TraceLog($"Loaded script module build record {JsonFile}");
					}
					catch(Exception Ex)
					{
						Log.TraceWarning($"[{JsonFile}] Failed to load build record: {Ex.Message}");
					}

					if (BuildRecord.ProjectPath != null)
					{
						LoadedBuildRecords.Add(FileReference.FromString(Path.GetFullPath(BuildRecord.ProjectPath, JsonFile.Directory.FullName)), (BuildRecord, JsonFile));
					}
					else
                    {
						// Delete the invalid build record
						Log.TraceWarning($"Deleting invalid build record {JsonFile}");
                    }
                }
			}

			return LoadedBuildRecords;
        }

		private static bool ValidateGlobbedFiles(DirectoryReference ProjectDirectory, 
			List<CsProjBuildRecord.Glob> Globs, HashSet<string> GlobbedDependencies, out string ValidationFailureMessage)
		{
			// First, evaluate globs
			
			// Files are grouped by ItemType (e.g. Compile, EmbeddedResource) to ensure that Exclude and
			// Remove act as expected.
			Dictionary<string, HashSet<string>> Files = new Dictionary<string, HashSet<string>>();
			foreach (CsProjBuildRecord.Glob Glob in Globs)
			{
				HashSet<string> TypedFiles;
				if (!Files.TryGetValue(Glob.ItemType, out TypedFiles))
				{
					TypedFiles = new HashSet<string>();
					Files.Add(Glob.ItemType, TypedFiles);
				}
				
				foreach (string IncludePath in Glob.Include)
				{
					TypedFiles.UnionWith(FileMatcher.Default.GetFiles(ProjectDirectory.FullName, IncludePath, Glob.Exclude));
				}

				foreach (string Remove in Glob.Remove)
				{
					// FileMatcher.IsMatch() doesn't handle inconsistent path separators correctly - which is why globs
					// are normalized when they are added to CsProjBuildRecord
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

		private static bool ValidateBuildRecord(CsProjBuildRecord BuildRecord, DirectoryReference ProjectDirectory, out string ValidationFailureMessage, 
			ref WriteTimeCache Cache)
		{
			string TargetRelativePath =
				Path.GetRelativePath(Unreal.EngineDirectory.FullName, BuildRecord.TargetPath);

			if (BuildRecord.Version != CsProjBuildRecord.CurrentVersion)
			{
				ValidationFailureMessage =
					$"version does not match: build record has version {BuildRecord.Version}; current version is {CsProjBuildRecord.CurrentVersion}";
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

		static void ValidateBuildRecordRecursively(
			Dictionary<FileReference, CsProjBuildRecord> ValidBuildRecords,
			Dictionary<FileReference, (CsProjBuildRecord, FileReference)> InvalidBuildRecords,
			Dictionary<FileReference, (CsProjBuildRecord, FileReference)> BuildRecords,
			FileReference ProjectPath, ref WriteTimeCache Cache)
		{
			if (ValidBuildRecords.ContainsKey(ProjectPath) || InvalidBuildRecords.ContainsKey(ProjectPath))
			{
				// Project validity has already been determined
				return;
			}

			// Was a build record loaded for this project path? (relevant when considering referenced projects)
			if (!BuildRecords.TryGetValue(ProjectPath, out (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath) Entry))
			{
				Log.TraceLog($"Found project {ProjectPath} with no existing build record");
				return;
			}

			// Is this particular build record valid?
			if (!ValidateBuildRecord(Entry.BuildRecord, ProjectPath.Directory, out string ValidationFailureMessage, ref Cache))
			{
				string AutomationProjectRelativePath = Path.GetRelativePath(Unreal.EngineDirectory.FullName, ProjectPath.FullName);
				Log.TraceLog($"[{AutomationProjectRelativePath}] {ValidationFailureMessage}");

				InvalidBuildRecords.Add(ProjectPath, Entry);
				return;
			}

			// Are all referenced build records valid?
			foreach (string ReferencedProjectPath in Entry.BuildRecord.ProjectReferences)
			{
				FileReference FullProjectPath = FileReference.FromString(Path.GetFullPath(ReferencedProjectPath, ProjectPath.Directory.FullName));
				ValidateBuildRecordRecursively(ValidBuildRecords, InvalidBuildRecords, BuildRecords, FullProjectPath, ref Cache);

				if (!ValidBuildRecords.ContainsKey(FullProjectPath))
				{
					string AutomationProjectRelativePath = Path.GetRelativePath(Unreal.EngineDirectory.FullName, ProjectPath.FullName);
					string DependencyRelativePath = Path.GetRelativePath(Unreal.EngineDirectory.FullName, FullProjectPath.FullName);
					Log.TraceLog($"[{AutomationProjectRelativePath}] Existing output is not valid because dependency {DependencyRelativePath} is not valid");
					InvalidBuildRecords.Add(ProjectPath, Entry);
					return;
				}

				// Ensure that the dependency was not built more recently than the project
				if (Entry.BuildRecord.TargetBuildTime < ValidBuildRecords[FullProjectPath].TargetBuildTime)
				{
					string AutomationProjectRelativePath = Path.GetRelativePath(Unreal.EngineDirectory.FullName, ProjectPath.FullName);
					string DependencyRelativePath = Path.GetRelativePath(Unreal.EngineDirectory.FullName, FullProjectPath.FullName);
					Log.TraceLog($"[{AutomationProjectRelativePath}] Existing output is not valid because dependency {DependencyRelativePath} is newer");
					InvalidBuildRecords.Add(ProjectPath, Entry);
					return;
				}
			}

			ValidBuildRecords.Add(ProjectPath, Entry.BuildRecord);
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

		static HashSet<FileReference> FindAutomationProjects(List<DirectoryReference> GameFolders, List<DirectoryReference> AdditionalScriptFolders)
		{
			
			return new HashSet<FileReference>(
				Rules.FindAllRulesSourceFiles(Rules.RulesFileType.AutomationModule,
				GameFolders: GameFolders, ForeignPlugins: null, AdditionalSearchPaths: AdditionalScriptFolders));
		}

		static List<DirectoryReference> GetGameDirectories(string ScriptsForProjectFileName)
        {
			List<DirectoryReference> GameDirectories = new List<DirectoryReference>();

			if (ScriptsForProjectFileName == null)
			{
				GameDirectories = NativeProjectsBase.EnumerateProjectFiles().Select(x => x.Directory).ToList();
			}
			else
			{
				DirectoryReference ScriptsDir = new DirectoryReference(Path.GetDirectoryName(ScriptsForProjectFileName));
				ScriptsDir = DirectoryReference.FindCorrectCase(ScriptsDir);
				GameDirectories.Add(ScriptsDir);
			}
			return GameDirectories;
        }

		static List<DirectoryReference> GetAdditionalDirectories(List<string> AdditionalScriptsFolders) =>
			AdditionalScriptsFolders == null ? new List<DirectoryReference>() :
				AdditionalScriptsFolders.Select(x => DirectoryReference.FindCorrectCase(new DirectoryReference(x))).ToList();

		static List<DirectoryReference> GetAdditionalBuildDirectories(List<DirectoryReference> GameDirectories) =>
			GameDirectories.Select(x => DirectoryReference.Combine(x, "Build")).Where(x => DirectoryReference.Exists(x)).ToList();


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

		private static HashSet<FileReference> BuildAllScriptPlugins(HashSet<FileReference> FoundAutomationProjects,
			bool bForceCompile, out bool bBuildSuccess, ref WriteTimeCache Cache, List<DirectoryReference> BaseDirectories)
		{
			Dictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference _)> BuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>();
			
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
			// out of date, and if building this project can be skipped. It is also used to populate Intermediate/ScriptModules after the
			// build completes
			foreach (Project Project in Projects.Values)
			{
				string TargetPath = Path.GetRelativePath(Project.DirectoryPath, Project.GetPropertyValue("TargetPath"));

				CsProjBuildRecord BuildRecord = new CsProjBuildRecord()
				{
					Version = CsProjBuildRecord.CurrentVersion,
					TargetPath = TargetPath,
					TargetBuildTime = Cache.GetLastWriteTime(DirectoryReference.FromString(Project.DirectoryPath), TargetPath),
					ProjectPath = Path.GetRelativePath(
						ConstructBuildRecordPath(FileReference.FromString(Project.FullPath), BaseDirectories).Directory.FullName, 
						Project.FullPath)
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

					List<string> Include = new List<string>(Glob.IncludeGlobs.Select(F => CleanGlobString(F))).OrderBy(x => x).ToList();
					List<string> Exclude = new List<string>(Glob.Excludes.Select(F => CleanGlobString(F))).OrderBy(x => x).ToList();
					List<string> Remove = new List<string>(Glob.Removes.Select(F => CleanGlobString(F))).OrderBy(x => x).ToList();
					
					BuildRecord.Globs.Add(new CsProjBuildRecord.Glob() { ItemType = Glob.ItemElement.ItemType, 
						Include = Include, Exclude = Exclude, Remove = Remove });
				}

				BuildRecords.Add(FileReference.FromString(Project.FullPath), (BuildRecord, default));
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
				Dictionary<FileReference, CsProjBuildRecord> ValidBuildRecords = new Dictionary<FileReference, CsProjBuildRecord>(BuildRecords.Count);
				Dictionary<FileReference, (CsProjBuildRecord, FileReference _)> InvalidBuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>(BuildRecords.Count);

				foreach (ProjectGraphNode Project in InputProjectGraph.ProjectNodesTopologicallySorted)
				{
					ValidateBuildRecordRecursively(ValidBuildRecords, InvalidBuildRecords, BuildRecords, FileReference.FromString(Project.ProjectInstance.FullPath), ref Cache);
				}

				// Select the projects that have been found to be out of date
				HashSet<ProjectGraphNode> OutOfDateProjects = new HashSet<ProjectGraphNode>(InputProjectGraph.ProjectNodes.Where(x => InvalidBuildRecords.Keys.Contains(FileReference.FromString(x.ProjectInstance.FullPath))));

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

				FileReference BuildRecordPath = ConstructBuildRecordPath(ProjectPath, BaseDirectories);

				CsProjBuildRecord BuildRecord = BuildRecords[ProjectPath].BuildRecord;

				// update target build times into build records to ensure everything is up-to-date
				FileReference FullPath = FileReference.Combine(ProjectPath.Directory, BuildRecord.TargetPath);
				BuildRecord.TargetBuildTime = FileReference.GetLastWriteTime(FullPath);

				if (FileReference.WriteAllTextIfDifferent(BuildRecordPath, 
					JsonSerializer.Serialize<CsProjBuildRecord>(BuildRecord, new JsonSerializerOptions { WriteIndented = true })))
                {
					Log.TraceLog($"Wrote script module build record to {BuildRecordPath}");
                }
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

			Log.TraceInformation($"Building {AutomationProjectGraph.EntryPointNodes.Count} projects (see Log 'Engine/Programs/AutomationTool/Saved/Logs/Log.txt' for more details)");
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

