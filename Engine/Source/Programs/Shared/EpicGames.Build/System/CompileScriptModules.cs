// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using Microsoft.Build.Shared;
using EpicGames.MsBuild;

namespace UnrealBuildBase
{
	public static class CompileScriptModule
	{
		class Hook : CsProjBuildHook
		{
			private Dictionary<string, DateTime> WriteTimes = new Dictionary<string, DateTime>();

			public DateTime GetLastWriteTime(DirectoryReference BasePath, string RelativeFilePath)
			{
				return GetLastWriteTime(BasePath.FullName, RelativeFilePath);
			}

			public DateTime GetLastWriteTime(string BasePath, string RelativeFilePath)
			{
				string NormalizedPath = Path.GetFullPath(RelativeFilePath, BasePath);
				if (!WriteTimes.TryGetValue(NormalizedPath, out DateTime WriteTime))
				{
					WriteTimes.Add(NormalizedPath, WriteTime = File.GetLastWriteTime(NormalizedPath));
				}
				return WriteTime;
			}

			public void ValidateRecursively(
				Dictionary<FileReference, CsProjBuildRecord> ValidBuildRecords,
				Dictionary<FileReference, (CsProjBuildRecord, FileReference?)> InvalidBuildRecords,
				Dictionary<FileReference, (CsProjBuildRecord, FileReference?)> BuildRecords,
				FileReference ProjectPath)
			{
				CompileScriptModule.ValidateBuildRecordRecursively(ValidBuildRecords, InvalidBuildRecords, BuildRecords, ProjectPath, this);
			}

			public bool HasWildcards(string FileSpec)
			{
				return FileMatcher.HasWildcards(FileSpec);
			}

			DirectoryReference CsProjBuildHook.EngineDirectory => Unreal.EngineDirectory;

			DirectoryReference CsProjBuildHook.DotnetDirectory => Unreal.DotnetDirectory;

			FileReference CsProjBuildHook.DotnetPath => Unreal.DotnetPath;
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
			CsProjBuildHook Hook = new Hook();

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
			Dictionary<FileReference, (CsProjBuildRecord, FileReference?)> ExistingBuildRecords = LoadExistingBuildRecords(BaseDirectories);

			Dictionary<FileReference, CsProjBuildRecord> ValidBuildRecords = new Dictionary<FileReference, CsProjBuildRecord>(ExistingBuildRecords.Count);
			Dictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference? BuildRecordPath)> InvalidBuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference?)>(ExistingBuildRecords.Count);

			if (bUseBuildRecords)
			{
				foreach (FileReference AutomationProject in FoundAutomationProjects)
				{
					ValidateBuildRecordRecursively(ValidBuildRecords, InvalidBuildRecords, ExistingBuildRecords, AutomationProject, Hook);
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
				foreach ((CsProjBuildRecord BuildRecord, FileReference BuildRecordPath) in ExistingBuildRecords.Values.Where(x => x.Item2 != null && x.Item2.HasExtension(".Automation.json")))
                {
					FileReference ProjectPath = FileReference.Combine(BuildRecordPath.Directory, BuildRecord.ProjectPath!);
					FileReference TargetPath = FileReference.Combine(ProjectPath.Directory, BuildRecord.TargetPath!);

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
				foreach ((CsProjBuildRecord _, FileReference? BuildRecordPath) in InvalidBuildRecords.Values)
				{
					if (BuildRecordPath != null)
					{
						Log.TraceLog($"Deleting invalid build record \"{BuildRecordPath}\"");
						FileReference.Delete(BuildRecordPath);
					}
				}
            }

			if (!bForceCompile && bUseBuildRecords)
			{
				// If all found records are valid, we can return their targets directly
				if (FoundAutomationProjects.All(x => ValidBuildRecords.ContainsKey(x)))
				{
					bBuildSuccess = true;
					return new HashSet<FileReference>(ValidBuildRecords.Select(x => FileReference.Combine(x.Key.Directory, x.Value.TargetPath!)));
                }
			}

			// Fall back to the slower approach: use msbuild to load csproj files & build as necessary
			return Build(FoundAutomationProjects, bForceCompile || !bUseBuildRecords, out bBuildSuccess, Hook, BaseDirectories);
		}

		/// <summary>
		/// This method exists purely to prevent EpicGames.MsBuild from being loaded until the absolute last moment.
		/// If it is placed in the caller directly, then when the caller is invoked, the assembly will be loaded resulting
		/// in the possible Microsoft.Build.Framework load issue later on is this method isn't invoked.
		/// </summary>
		static HashSet<FileReference> Build(HashSet<FileReference> FoundAutomationProjects,
			bool bForceCompile, out bool bBuildSuccess, CsProjBuildHook Hook, List<DirectoryReference> BaseDirectories)
		{
			return CsProjBuilder.Build(FoundAutomationProjects, bForceCompile, out bBuildSuccess, Hook, BaseDirectories);
		}

		/// <summary>
		/// Find and load existing build record .json files from any Intermediate/ScriptModules found in the provided lists
		/// </summary>
		/// <param name="BaseDirectories"></param>
		/// <returns></returns>
		static Dictionary<FileReference, (CsProjBuildRecord, FileReference?)> LoadExistingBuildRecords(List<DirectoryReference> BaseDirectories)
        {
			Dictionary<FileReference, (CsProjBuildRecord, FileReference?)> LoadedBuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference?)>();

			foreach (DirectoryReference Directory in BaseDirectories)
			{
				DirectoryReference IntermediateDirectory = DirectoryReference.Combine(Directory, "Intermediate", "ScriptModules");
				if (!DirectoryReference.Exists(IntermediateDirectory))
				{
					continue;
				}

				foreach (FileReference JsonFile in DirectoryReference.EnumerateFiles(IntermediateDirectory, "*.json"))
                {
					CsProjBuildRecord? BuildRecord = default;

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

					if (BuildRecord != null && BuildRecord.ProjectPath != null)
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
				HashSet<string>? TypedFiles;
				if (!Files.TryGetValue(Glob.ItemType!, out TypedFiles))
				{
					TypedFiles = new HashSet<string>();
					Files.Add(Glob.ItemType!, TypedFiles);
				}
				
				foreach (string IncludePath in Glob.Include!)
				{
					TypedFiles.UnionWith(FileMatcher.Default.GetFiles(ProjectDirectory.FullName, IncludePath, Glob.Exclude));
				}

				foreach (string Remove in Glob.Remove!)
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

		private static bool ValidateBuildRecord(CsProjBuildRecord BuildRecord, DirectoryReference ProjectDirectory, out string ValidationFailureMessage, CsProjBuildHook Hook)
		{
			string TargetRelativePath =
				Path.GetRelativePath(Unreal.EngineDirectory.FullName, BuildRecord.TargetPath!);

			if (BuildRecord.Version != CsProjBuildRecord.CurrentVersion)
			{
				ValidationFailureMessage =
					$"version does not match: build record has version {BuildRecord.Version}; current version is {CsProjBuildRecord.CurrentVersion}";
				return false;
			}

			DateTime TargetWriteTime = Hook.GetLastWriteTime(ProjectDirectory, BuildRecord.TargetPath!);

			if (BuildRecord.TargetBuildTime != TargetWriteTime)
			{
				ValidationFailureMessage =
					$"recorded target build time ({BuildRecord.TargetBuildTime}) does not match {TargetRelativePath} ({TargetWriteTime})";
				return false;
			}

			foreach (string Dependency in BuildRecord.Dependencies)
			{
				if (Hook.GetLastWriteTime(ProjectDirectory, Dependency) > TargetWriteTime)
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
				if (Hook.GetLastWriteTime(ProjectDirectory, Dependency) > TargetWriteTime)
				{
					ValidationFailureMessage = $"{Dependency} is newer than {TargetRelativePath}";
					return false;
				}
			}

			return true;
		}

		static void ValidateBuildRecordRecursively(
			Dictionary<FileReference, CsProjBuildRecord> ValidBuildRecords,
			Dictionary<FileReference, (CsProjBuildRecord, FileReference?)> InvalidBuildRecords,
			Dictionary<FileReference, (CsProjBuildRecord, FileReference?)> BuildRecords,
			FileReference ProjectPath, CsProjBuildHook Hook)
		{
			if (ValidBuildRecords.ContainsKey(ProjectPath) || InvalidBuildRecords.ContainsKey(ProjectPath))
			{
				// Project validity has already been determined
				return;
			}

			// Was a build record loaded for this project path? (relevant when considering referenced projects)
			if (!BuildRecords.TryGetValue(ProjectPath, out (CsProjBuildRecord BuildRecord, FileReference? BuildRecordPath) Entry))
			{
				Log.TraceLog($"Found project {ProjectPath} with no existing build record");
				return;
			}

			// Is this particular build record valid?
			if (!ValidateBuildRecord(Entry.BuildRecord, ProjectPath.Directory, out string ValidationFailureMessage, Hook))
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
				ValidateBuildRecordRecursively(ValidBuildRecords, InvalidBuildRecords, BuildRecords, FullProjectPath, Hook);

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

		static List<DirectoryReference> GetGameDirectories(string ScriptsForProjectFileName)
        {
			List<DirectoryReference> GameDirectories = new List<DirectoryReference>();

			if (ScriptsForProjectFileName == null)
			{
				GameDirectories = NativeProjectsBase.EnumerateProjectFiles().Select(x => x.Directory).ToList();
			}
			else
			{
				DirectoryReference ScriptsDir = new DirectoryReference(Path.GetDirectoryName(ScriptsForProjectFileName)!);
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
    }
}

