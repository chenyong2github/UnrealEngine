// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	public class TargetReceipt
	{
		public string? Configuration { get; set; }
		public string? Launch { get; set; }
		public string? LaunchCmd { get; set; }

		public static bool TryRead(FileReference Location, DirectoryReference? EngineDir, DirectoryReference? ProjectDir, [NotNullWhen(true)] out TargetReceipt? Receipt)
		{
			if (Utility.TryLoadJson(Location, out Receipt))
			{
				Receipt.Launch = ExpandReceiptVariables(Receipt.Launch, EngineDir, ProjectDir);
				Receipt.LaunchCmd = ExpandReceiptVariables(Receipt.LaunchCmd, EngineDir, ProjectDir);
				return true;
			}
			return false;
		}

		[return: NotNullIfNotNull("Line")]
		private static string? ExpandReceiptVariables(string? Line, DirectoryReference? EngineDir, DirectoryReference? ProjectDir)
		{
			string? ExpandedLine = Line;
			if (ExpandedLine != null)
			{
				if (EngineDir != null)
				{
					ExpandedLine = ExpandedLine.Replace("$(EngineDir)", EngineDir.FullName);
				}
				if (ProjectDir != null)
				{
					ExpandedLine = ExpandedLine.Replace("$(ProjectDir)", ProjectDir.FullName);
				}
			}
			return ExpandedLine;
		}
	}

	public static class ConfigUtils
	{
		public static string HostPlatform { get; } = GetHostPlatform();

		public static string HostArchitectureSuffix { get; } = String.Empty;

		static string GetHostPlatform()
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				return "Win64";
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			{
				return "Mac";
			}
			else
			{
				return "Linux";
			}
		}

		public static Task<ConfigFile> ReadProjectConfigFileAsync(IPerforceConnection Perforce, ProjectInfo ProjectInfo, ILogger Logger, CancellationToken CancellationToken)
		{
			return ReadProjectConfigFileAsync(Perforce, ProjectInfo, new List<KeyValuePair<FileReference, DateTime>>(), Logger, CancellationToken);
		}

		public static Task<ConfigFile> ReadProjectConfigFileAsync(IPerforceConnection Perforce, ProjectInfo ProjectInfo, List<KeyValuePair<FileReference, DateTime>> LocalConfigFiles, ILogger Logger, CancellationToken CancellationToken)
		{
			return ReadProjectConfigFileAsync(Perforce, ProjectInfo.ClientRootPath, ProjectInfo.ClientFileName, ProjectInfo.CacheFolder, LocalConfigFiles, Logger, CancellationToken);
		}

		public static async Task<ConfigFile> ReadProjectConfigFileAsync(IPerforceConnection Perforce, string BranchClientPath, string SelectedClientFileName, DirectoryReference CacheFolder, List<KeyValuePair<FileReference, DateTime>> LocalConfigFiles, ILogger Logger, CancellationToken CancellationToken)
		{
			List<string> ConfigFilePaths = Utility.GetDepotConfigPaths(BranchClientPath + "/Engine", SelectedClientFileName);

			ConfigFile ProjectConfig = new ConfigFile();

			List<PerforceResponse<FStatRecord>> Responses = await Perforce.TryFStatAsync(FStatOptions.IncludeFileSizes, ConfigFilePaths, CancellationToken).ToListAsync(CancellationToken);
			foreach (PerforceResponse<FStatRecord> Response in Responses)
			{
				if (Response.Succeeded)
				{
					string[]? Lines = null;

					// Skip file records which are still in the workspace, but were synced from a different branch. For these files, the action seems to be empty, so filter against that.
					FStatRecord FileRecord = Response.Data;
					if (FileRecord.HeadAction == FileAction.None)
					{
						continue;
					}

					// If this file is open for edit, read the local version
					string? LocalFileName = FileRecord.ClientFile;
					if (LocalFileName != null && File.Exists(LocalFileName) && (File.GetAttributes(LocalFileName) & FileAttributes.ReadOnly) == 0)
					{
						try
						{
							DateTime LastModifiedTime = File.GetLastWriteTimeUtc(LocalFileName);
							LocalConfigFiles.Add(new KeyValuePair<FileReference, DateTime>(new FileReference(LocalFileName), LastModifiedTime));
							Lines = await File.ReadAllLinesAsync(LocalFileName, CancellationToken);
						}
						catch (Exception Ex)
						{
							Logger.LogInformation(Ex, "Failed to read local config file for {Path}", LocalFileName);
						}
					}

					// Otherwise try to get it from perforce
					if (Lines == null && FileRecord.DepotFile != null)
					{
						Lines = await Utility.TryPrintFileUsingCacheAsync(Perforce, FileRecord.DepotFile, CacheFolder, FileRecord.Digest, Logger, CancellationToken);
					}

					// Merge the text with the config file
					if (Lines != null)
					{
						try
						{
							ProjectConfig.Parse(Lines.ToArray());
							Logger.LogDebug("Read config file from {DepotFile}", FileRecord.DepotFile);
						}
						catch (Exception Ex)
						{
							Logger.LogInformation(Ex, "Failed to read config file from {DepotFile}", FileRecord.DepotFile);
						}
					}
				}
			}
			return ProjectConfig;
		}

		public static FileReference GetEditorTargetFile(ProjectInfo ProjectInfo, ConfigFile ProjectConfig)
		{
			if (ProjectInfo.ProjectPath.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
			{
				List<FileReference> TargetFiles = FindTargets(ProjectInfo.LocalFileName.Directory);

				FileReference? TargetFile = TargetFiles.OrderBy(x => x.FullName, StringComparer.OrdinalIgnoreCase).FirstOrDefault(x => x.FullName.EndsWith("Editor.target.cs", StringComparison.OrdinalIgnoreCase));
				if (TargetFile != null)
				{
					return TargetFile;
				}
			}

			string DefaultEditorTargetName = GetDefaultEditorTargetName(ProjectInfo, ProjectConfig);
			return FileReference.Combine(ProjectInfo.LocalRootPath, "Engine", "Source", $"{DefaultEditorTargetName}.Target.cs");
		}

		public static FileReference GetEditorReceiptFile(ProjectInfo ProjectInfo, ConfigFile ProjectConfig, BuildConfig Config)
		{
			FileReference TargetFile = GetEditorTargetFile(ProjectInfo, ProjectConfig);
			return GetReceiptFile(ProjectInfo, TargetFile, Config.ToString());
		}

		private static List<FileReference> FindTargets(DirectoryReference EngineOrProjectDir)
		{
			List<FileReference> Targets = new List<FileReference>();

			DirectoryReference SourceDir = DirectoryReference.Combine(EngineOrProjectDir, "Source");
			if (DirectoryReference.Exists(SourceDir))
			{
				foreach (FileReference TargetFile in DirectoryReference.EnumerateFiles(SourceDir))
				{
					const string Extension = ".target.cs";
					if (TargetFile.FullName.EndsWith(Extension, StringComparison.OrdinalIgnoreCase))
					{
						Targets.Add(TargetFile);
					}
				}
			}

			return Targets;
		}

		public static string GetDefaultEditorTargetName(ProjectInfo ProjectInfo, ConfigFile ProjectConfigFile)
		{
			string? EditorTarget;
			if (!TryGetProjectSetting(ProjectConfigFile, ProjectInfo.ProjectIdentifier, "EditorTarget", out EditorTarget))
			{
				if (ProjectInfo.bIsEnterpriseProject)
				{
					EditorTarget = "StudioEditor";
				}
				else
				{
					EditorTarget = "UE4Editor";
				}
			}
			return EditorTarget;
		}

		public static bool TryReadEditorReceipt(ProjectInfo ProjectInfo, FileReference ReceiptFile, [NotNullWhen(true)] out TargetReceipt? Receipt)
		{
			DirectoryReference EngineDir = DirectoryReference.Combine(ProjectInfo.LocalRootPath, "Engine");
			DirectoryReference ProjectDir = ProjectInfo.LocalFileName.Directory;

			if (ReceiptFile.IsUnderDirectory(ProjectDir))
			{
				return TargetReceipt.TryRead(ReceiptFile, EngineDir, ProjectDir, out Receipt);
			}
			else
			{
				return TargetReceipt.TryRead(ReceiptFile, EngineDir, null, out Receipt);
			}
		}

		public static TargetReceipt CreateDefaultEditorReceipt(ProjectInfo ProjectInfo, ConfigFile ProjectConfigFile, BuildConfig Configuration)
		{
			string BaseName = GetDefaultEditorTargetName(ProjectInfo, ProjectConfigFile);
			if (Configuration != BuildConfig.Development || !String.IsNullOrEmpty(HostArchitectureSuffix))
			{
				if (Configuration != BuildConfig.DebugGame || ProjectConfigFile.GetValue("Options.DebugGameHasSeparateExecutable", false))
				{
					BaseName += $"-{HostPlatform}-{Configuration}{HostArchitectureSuffix}";
				}
			}

			string Extension = String.Empty;
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				Extension = ".exe";
			}

			TargetReceipt Receipt = new TargetReceipt();
			Receipt.Configuration = Configuration.ToString();
			Receipt.Launch = FileReference.Combine(ProjectInfo.LocalRootPath, "Engine", "Binaries", HostPlatform, $"{BaseName}{Extension}").FullName;
			Receipt.LaunchCmd = FileReference.Combine(ProjectInfo.LocalRootPath, "Engine", "Binaries", HostPlatform, $"{BaseName}-Cmd{Extension}").FullName;
			return Receipt;
		}

		public static FileReference GetReceiptFile(ProjectInfo ProjectInfo, FileReference TargetFile, string Configuration)
		{
			string TargetName = TargetFile.GetFileNameWithoutAnyExtensions();

			DirectoryReference? ProjectDir = ProjectInfo.ProjectDir;
			if (ProjectDir != null)
			{
				return GetReceiptFile(ProjectDir, TargetName, Configuration);
			}
			else
			{
				return GetReceiptFile(ProjectInfo.EngineDir, TargetName, Configuration);
			}
		}

		public static FileReference GetReceiptFile(DirectoryReference BaseDir, string TargetName, string Configuration)
		{
			return GetReceiptFile(BaseDir, TargetName, HostPlatform, Configuration, HostArchitectureSuffix);
		}

		public static FileReference GetReceiptFile(DirectoryReference BaseDir, string TargetName, string Platform, string Configuration, string ArchitectureSuffix)
		{
			if (String.IsNullOrEmpty(ArchitectureSuffix) && Configuration.Equals("Development", StringComparison.OrdinalIgnoreCase))
			{
				return FileReference.Combine(BaseDir, "Binaries", Platform, $"{TargetName}.target");
			}
			else
			{
				return FileReference.Combine(BaseDir, "Binaries", Platform, $"{TargetName}-{Platform}-{Configuration}{ArchitectureSuffix}.target");
			}
		}

		public static Dictionary<Guid, ConfigObject> GetDefaultBuildStepObjects(ProjectInfo ProjectInfo, string EditorTarget, BuildConfig EditorConfig, ConfigFile LatestProjectConfigFile, bool ShouldSyncPrecompiledEditor)
		{
			string ProjectArgument = "";
			if (ProjectInfo.LocalFileName.HasExtension(".uproject"))
			{
				ProjectArgument = String.Format("\"{0}\"", ProjectInfo.LocalFileName);
			}

			bool bUseCrashReportClientEditor = LatestProjectConfigFile.GetValue("Options.UseCrashReportClientEditor", false);

			string HostPlatform;
			if(RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			{
				HostPlatform = "Mac";
			}
			else if(RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				HostPlatform = "Linux";
			}
			else
			{
				HostPlatform = "Win64";
			}

			List<BuildStep> DefaultBuildSteps = new List<BuildStep>();
			DefaultBuildSteps.Add(new BuildStep(new Guid("{01F66060-73FA-4CC8-9CB3-E217FBBA954E}"), 0, "Compile UnrealHeaderTool", "Compiling UnrealHeaderTool...", 1, "UnrealHeaderTool", HostPlatform, "Development", "", !ShouldSyncPrecompiledEditor));
			DefaultBuildSteps.Add(new BuildStep(new Guid("{F097FF61-C916-4058-8391-35B46C3173D5}"), 1, $"Compile {EditorTarget}", $"Compiling {EditorTarget}...", 10, EditorTarget, HostPlatform, EditorConfig.ToString(), ProjectArgument, !ShouldSyncPrecompiledEditor));
			DefaultBuildSteps.Add(new BuildStep(new Guid("{C6E633A1-956F-4AD3-BC95-6D06D131E7B4}"), 2, "Compile ShaderCompileWorker", "Compiling ShaderCompileWorker...", 1, "ShaderCompileWorker", HostPlatform, "Development", "", !ShouldSyncPrecompiledEditor));
			DefaultBuildSteps.Add(new BuildStep(new Guid("{24FFD88C-7901-4899-9696-AE1066B4B6E8}"), 3, "Compile UnrealLightmass", "Compiling UnrealLightmass...", 1, "UnrealLightmass", HostPlatform, "Development", "", !ShouldSyncPrecompiledEditor));
			DefaultBuildSteps.Add(new BuildStep(new Guid("{FFF20379-06BF-4205-8A3E-C53427736688}"), 4, "Compile CrashReportClient", "Compiling CrashReportClient...", 1, "CrashReportClient", HostPlatform, "Shipping", "", !ShouldSyncPrecompiledEditor && !bUseCrashReportClientEditor));
			DefaultBuildSteps.Add(new BuildStep(new Guid("{7143D861-58D3-4F83-BADC-BC5DCB2079F6}"), 5, "Compile CrashReportClientEditor", "Compiling CrashReportClientEditor...", 1, "CrashReportClientEditor", HostPlatform, "Shipping", "", !ShouldSyncPrecompiledEditor && bUseCrashReportClientEditor));

			return DefaultBuildSteps.ToDictionary(x => x.UniqueId, x => x.ToConfigObject());
		}

		public static Dictionary<string, string> GetWorkspaceVariables(ProjectInfo ProjectInfo, int ChangeNumber, int CodeChangeNumber, TargetReceipt? EditorTarget, ConfigFile? ProjectConfigFile)
		{
			Dictionary<string, string> Variables = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

			if (ProjectInfo.StreamName != null)
			{
				Variables.Add("Stream", ProjectInfo.StreamName);
			}

			Variables.Add("Change", ChangeNumber.ToString());
			Variables.Add("CodeChange", CodeChangeNumber.ToString());

			Variables.Add("ClientName", ProjectInfo.ClientName);
			Variables.Add("BranchDir", ProjectInfo.LocalRootPath.FullName);
			Variables.Add("ProjectDir", ProjectInfo.LocalFileName.Directory.FullName);
			Variables.Add("ProjectFile", ProjectInfo.LocalFileName.FullName);
			Variables.Add("UseIncrementalBuilds", "1");

			string EditorConfig = EditorTarget?.Configuration ?? String.Empty;
			Variables.Add("EditorConfig", EditorConfig);

			string EditorLaunch = EditorTarget?.Launch ?? String.Empty;
			Variables.Add("EditorExe", EditorLaunch);

			string EditorLaunchCmd = EditorTarget?.LaunchCmd ?? EditorLaunch.Replace(".exe", "-Cmd.exe");
			Variables.Add("EditorCmdExe", EditorLaunchCmd);

			// Legacy
			Variables.Add("UE4EditorConfig", EditorConfig);
			Variables.Add("UE4EditorDebugArg", (EditorConfig.Equals("Debug", StringComparison.Ordinal) || EditorConfig.Equals("DebugGame", StringComparison.Ordinal)) ? " -debug" : "");
			Variables.Add("UE4EditorExe", EditorLaunch);
			Variables.Add("UE4EditorCmdExe", EditorLaunchCmd);

			if (ProjectConfigFile != null)
			{
				if (TryGetProjectSetting(ProjectConfigFile, ProjectInfo.ProjectIdentifier, "SdkInstallerDir", out string? SdkInstallerDir))
				{
					Variables.Add("SdkInstallerDir", SdkInstallerDir);
				}
			}

			return Variables;
		}

		public static Dictionary<string, string> GetWorkspaceVariables(ProjectInfo ProjectInfo, int ChangeNumber, int CodeChangeNumber, TargetReceipt? EditorTarget, ConfigFile? ProjectConfigFile, IEnumerable<KeyValuePair<string, string>> AdditionalVariables)
		{
			Dictionary<string, string> Variables = GetWorkspaceVariables(ProjectInfo, ChangeNumber, CodeChangeNumber, EditorTarget, ProjectConfigFile);
			foreach ((string Key, string Value) in AdditionalVariables)
			{
				Variables[Key] = Value;
			}
			return Variables;
		}

		public static bool TryGetProjectSetting(ConfigFile ProjectConfigFile, string SelectedProjectIdentifier, string Name, [NotNullWhen(true)] out string? Value)
		{
			string Path = SelectedProjectIdentifier;
			for (; ; )
			{
				ConfigSection ProjectSection = ProjectConfigFile.FindSection(Path);
				if (ProjectSection != null)
				{
					string? NewValue = ProjectSection.GetValue(Name, null);
					if (NewValue != null)
					{
						Value = NewValue;
						return true;
					}
				}

				int LastSlash = Path.LastIndexOf('/');
				if (LastSlash < 2)
				{
					break;
				}

				Path = Path.Substring(0, LastSlash);
			}

			ConfigSection DefaultSection = ProjectConfigFile.FindSection("Default");
			if (DefaultSection != null)
			{
				string? NewValue = DefaultSection.GetValue(Name, null);
				if (NewValue != null)
				{
					Value = NewValue;
					return true;
				}
			}

			Value = null;
			return false;
		}

		public static Dictionary<Guid, WorkspaceSyncCategory> GetSyncCategories(ConfigFile ProjectConfigFile)
		{
			Dictionary<Guid, WorkspaceSyncCategory> UniqueIdToCategory = new Dictionary<Guid, WorkspaceSyncCategory>();
			if (ProjectConfigFile != null)
			{
				string[] CategoryLines = ProjectConfigFile.GetValues("Options.SyncCategory", new string[0]);
				foreach (string CategoryLine in CategoryLines)
				{
					ConfigObject Object = new ConfigObject(CategoryLine);

					Guid UniqueId;
					if (Guid.TryParse(Object.GetValue("UniqueId", ""), out UniqueId))
					{
						WorkspaceSyncCategory? Category;
						if (!UniqueIdToCategory.TryGetValue(UniqueId, out Category))
						{
							Category = new WorkspaceSyncCategory(UniqueId);
							UniqueIdToCategory.Add(UniqueId, Category);
						}

						if (Object.GetValue("Clear", false))
						{
							Category.Paths = new string[0];
							Category.Requires = new Guid[0];
						}

						Category.Name = Object.GetValue("Name", Category.Name);
						Category.bEnable = Object.GetValue("Enable", Category.bEnable);
						Category.Paths = Enumerable.Concat(Category.Paths, Object.GetValue("Paths", "").Split(';').Select(x => x.Trim())).Where(x => x.Length > 0).Distinct().OrderBy(x => x).ToArray();
						Category.bHidden = Object.GetValue("Hidden", Category.bHidden);
						Category.Requires = Enumerable.Concat(Category.Requires, ParseGuids(Object.GetValue("Requires", "").Split(';'))).Distinct().OrderBy(x => x).ToArray();
					}
				}
			}
			return UniqueIdToCategory;
		}

		static IEnumerable<Guid> ParseGuids(IEnumerable<string> Values)
		{
			foreach (string Value in Values)
			{
				Guid Guid;
				if (Guid.TryParse(Value, out Guid))
				{
					yield return Guid;
				}
			}
		}
	}
}
