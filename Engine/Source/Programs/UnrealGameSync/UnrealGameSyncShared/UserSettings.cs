// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	public enum BuildConfig
	{
		Debug,
		DebugGame,
		Development,
	}

	public enum TabLabels
	{
		Stream,
		WorkspaceName,
		WorkspaceRoot,
		ProjectFile,
	}

	public enum BisectState
	{
		Include,
		Exclude,
		Pass,
		Fail,
	}

	public enum UserSelectedProjectType
	{
		Client,
		Local
	}

	public enum FilterType
	{
		None,
		Code,
		Content
	}

	public enum LatestChangeType
	{
		Any,
		Good,
		Starred,
	}

	public enum UserSettingsVersion
	{
		Initial = 0,
		DefaultServerSettings = 1,
		XgeShaderCompilation = 2,
		DefaultNumberOfThreads = 3,
		Latest = DefaultNumberOfThreads
	}

	public class ArchiveSettings
	{
		public bool bEnabled;
		public string Type;
		public List<string> Order;

		public ArchiveSettings(bool bEnabled, string Type, IEnumerable<string> Order)
		{
			this.bEnabled = bEnabled;
			this.Type = Type;
			this.Order = new List<string>(Order);
		}

		public static bool TryParseConfigEntry(string Text, [NotNullWhen(true)] out ArchiveSettings? Settings)
		{
			ConfigObject Object = new ConfigObject(Text);

			string? Type = Object.GetValue("Type", null);
			if (Type == null)
			{
				Settings = null;
				return false;
			}
			else
			{
				string[] Order = Object.GetValue("Order", "").Split(new char[] { ';' }, StringSplitOptions.RemoveEmptyEntries);
				bool bEnabled = Object.GetValue("Enabled", 0) != 0;

				Settings = new ArchiveSettings(bEnabled, Type, Order);
				return true;
			}
		}

		public string ToConfigEntry()
		{
			ConfigObject Object = new ConfigObject();

			Object.SetValue("Enabled", bEnabled ? 1 : 0);
			Object.SetValue("Type", Type);
			Object.SetValue("Order", String.Join(";", Order));

			return Object.ToString();
		}

		public override string ToString()
		{
			return ToConfigEntry();
		}
	}

	public class UserSelectedProjectSettings
	{
		[JsonIgnore]
		FileReference? File { get; set; }

		public string? ServerAndPort { get; set; }
		public string? UserName { get; set; }
		public UserSelectedProjectType Type { get; set; }
		public string? ClientPath { get; set; }
		public string? LocalPath { get; set; }

		public UserSelectedProjectSettings(string? ServerAndPort, string? UserName, UserSelectedProjectType Type, string? ClientPath, string? LocalPath)
		{
			this.ServerAndPort = ServerAndPort;
			this.UserName = UserName;
			this.Type = Type;
			this.ClientPath = ClientPath;
			this.LocalPath = LocalPath;
		}

		public static bool TryParseConfigEntry(string Text, [NotNullWhen(true)] out UserSelectedProjectSettings? Project)
		{
			ConfigObject Object = new ConfigObject(Text);

			UserSelectedProjectType Type;
			if(Enum.TryParse(Object.GetValue("Type", ""), out Type))
			{
				string? ServerAndPort = Object.GetValue("ServerAndPort", null);
				if(String.IsNullOrWhiteSpace(ServerAndPort))
				{
					ServerAndPort = null;
				}

				// Fixup for code that was saving server host name rather than DNS entry
				if(ServerAndPort != null && ServerAndPort.Equals("p4-nodeb.epicgames.net:1666", StringComparison.OrdinalIgnoreCase))
				{
					ServerAndPort = "perforce:1666";
				}

				string? UserName = Object.GetValue("UserName", null);
				if(String.IsNullOrWhiteSpace(UserName))
				{
					UserName = null;
				}

				string? LocalPath = Object.GetValue("LocalPath", null);
				if(String.IsNullOrWhiteSpace(LocalPath))
				{
					LocalPath = null;
				}

				string? ClientPath = Object.GetValue("ClientPath", null);
				if(String.IsNullOrWhiteSpace(ClientPath))
				{
					ClientPath = null;
				}

				if((Type == UserSelectedProjectType.Client && ClientPath != null) || (Type == UserSelectedProjectType.Local && LocalPath != null))
				{
					Project = new UserSelectedProjectSettings(ServerAndPort, UserName, Type, ClientPath, LocalPath);
					return true;
				}
			}

			Project = null;
			return false;
		}

		public string ToConfigEntry()
		{
			ConfigObject Object = new ConfigObject();

			if(ServerAndPort != null)
			{
				Object.SetValue("ServerAndPort", ServerAndPort);
			}
			if(UserName != null)
			{
				Object.SetValue("UserName", UserName);
			}

			Object.SetValue("Type", Type.ToString());

			if(ClientPath != null)
			{
				Object.SetValue("ClientPath", ClientPath);
			}
			if(LocalPath != null)
			{
				Object.SetValue("LocalPath", LocalPath);
			}

			return Object.ToString();
		}

		public override string? ToString()
		{
			return LocalPath ?? ClientPath;
		}
	}

	public class SyncCategory
	{
		public Guid Id { get; set; }
		public bool Enable { get; set; }
	}

	public class BisectEntry
	{
		public int Change { get; set; }
		public BisectState State { get; set; }
	}

	public class UserWorkspaceSettings
	{
		[JsonIgnore]
		public DirectoryReference RootDir { get; set; } = null!;

		// Connection settings
		public string? ServerAndPort { get; set; }
		public string? UserName { get; set; }
		public string ClientName { get; set; } = String.Empty;

		// Path to the root of the branch within this client, with a trailing slash if non-empty
		public string BranchPath { get; set; } = String.Empty;

		// The currently selected project
		public string ProjectPath { get; set; } = String.Empty;

		// Workspace specific SyncFilters
		public string[] SyncView { get; set; } = Array.Empty<string>();
		public List<SyncCategory> SyncCategories { get; set; } = new List<SyncCategory>();
		public bool? bSyncAllProjects { get; set; }
		public bool? bIncludeAllProjectsInSolution { get; set; }

		[JsonIgnore]
		public Dictionary<Guid, bool> SyncCategoriesDict
		{
			get => SyncCategories.ToDictionary(x => x.Id, x => x.Enable);
			set => SyncCategories = value.Select(x => new SyncCategory { Id = x.Key, Enable = x.Value }).ToList();
		}

		[JsonIgnore]
		public FileReference ConfigFile => GetConfigFile(RootDir);

		[JsonIgnore]
		public string ClientProjectPath => $"//{ClientName}{BranchPath}{ProjectPath}";

		public static bool TryLoad(DirectoryReference RootDir, [NotNullWhen(true)] out UserWorkspaceSettings? Settings)
		{
			FileReference ConfigFile = GetConfigFile(RootDir);
			if (Utility.TryLoadJson(ConfigFile, out Settings))
			{
				Settings.RootDir = RootDir;
				return true;
			}
			else
			{
				Settings = null;
				return false;
			}
		}

		static object SyncRoot = new object();

		public void Save()
		{
			lock (SyncRoot)
			{
				Utility.SaveJson(ConfigFile, this);
			}
		}

		public static FileReference GetConfigFile(DirectoryReference RootDir)
		{
			return FileReference.Combine(UserSettings.GetConfigDir(RootDir), "settings.json");
		}
	}

	public class UserWorkspaceState
	{
		[JsonIgnore]
		public DirectoryReference RootDir { get; set; } = null!;

		// Settings for the currently synced project in this workspace. CurrentChangeNumber is only valid for this workspace if CurrentProjectPath is the current project.
		public string? CurrentProjectIdentifier { get; set; }
		public int CurrentChangeNumber { get; set; } = -1;
		public string? CurrentSyncFilterHash { get; set; }
		public List<int> AdditionalChangeNumbers { get; set; } = new List<int>();

		// Settings for the last attempted sync. These values are set to persist error messages between runs.
		public int LastSyncChangeNumber { get; set; }
		public WorkspaceUpdateResult LastSyncResult { get; set; }
		public string? LastSyncResultMessage { get; set; }
		public DateTime? LastSyncTime { get; set; }
		public int LastSyncDurationSeconds { get; set; }

		// The last successful build, regardless of whether a failed sync has happened in the meantime. Used to determine whether to force a clean due to entries in the project config file.
		public int LastBuiltChangeNumber { get; set; }

		// Expanded archives in the workspace
		public string[]? ExpandedArchiveTypes { get; set; }

		// The changes that we're regressing at the moment
		public List<BisectEntry> BisectChanges { get; set; } = new List<BisectEntry>();

		// Path to the config file
		public FileReference ConfigFile => GetConfigFile(RootDir);

		public void SetBisectState(int Change, BisectState State)
		{
			BisectEntry Entry = BisectChanges.FirstOrDefault(x => x.Change == Change);
			if (Entry == null)
			{
				Entry = new BisectEntry();
				Entry.Change = Change;
				BisectChanges.Add(Entry);
			}
			Entry.State = State;
		}

		public static bool TryLoad(DirectoryReference RootDir, [NotNullWhen(true)] out UserWorkspaceState? State)
		{
			FileReference ConfigFile = GetConfigFile(RootDir);
			if (Utility.TryLoadJson(ConfigFile, out State))
			{
				State.RootDir = RootDir;
				return true;
			}
			else
			{
				State = null;
				return false;
			}
		}

		static object SyncRoot = new object();

		public void Save()
		{
			lock (SyncRoot)
			{
				Utility.SaveJson(ConfigFile, this);
			}
		}

		public static FileReference GetConfigFile(DirectoryReference RootDir) => FileReference.Combine(UserSettings.GetConfigDir(RootDir), "state.json");
	}

	public class UserProjectSettings
	{
		[JsonIgnore]
		public FileReference? File { get; set; }

		public List<ConfigObject> BuildSteps { get; set; } = new List<ConfigObject>();
		public FilterType FilterType { get; set; }
		public HashSet<string> FilterBadges { get; set; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

		public void Save() => Utility.SaveJson(File!, this);
	}

	public class UserSettings
	{
		/// <summary>
		/// Enum that decribes which robomerge changes to show
		/// </summary>
		public enum RobomergeShowChangesOption
		{
			All,		// Show all changes from robomerge
			Badged,		// Show only robomerge changes that have an associated badge
			None		// Show no robomerge changes
		};

		FileReference FileName;
		ConfigFile ConfigFile = new ConfigFile();

		// General settings
		public UserSettingsVersion Version = UserSettingsVersion.Latest;
		public bool bBuildAfterSync;
		public bool bRunAfterSync;
		public bool bOpenSolutionAfterSync;
		public bool bShowLogWindow;
		public bool bAutoResolveConflicts;
		public bool bShowUnreviewedChanges;
		public bool bShowAutomatedChanges;
		public RobomergeShowChangesOption ShowRobomerge;
		public bool bAnnotateRobmergeChanges;
		public bool bShowLocalTimes;
		public bool bKeepInTray;
		public Guid[] EnabledTools;
		public bool bShowNetCoreInfo;
		public int FilterIndex;
		public UserSelectedProjectSettings? LastProject;
		public List<UserSelectedProjectSettings> OpenProjects;
		public List<UserSelectedProjectSettings> RecentProjects;
		public string[] SyncView;
		public Dictionary<Guid, bool> SyncCategories;
		public bool bSyncAllProjects;
		public bool bIncludeAllProjectsInSolution;
		public LatestChangeType SyncType;
		public BuildConfig CompiledEditorBuildConfig; // NB: This assumes not using precompiled editor. See CurrentBuildConfig.
		public TabLabels TabLabels;

		// Precompiled binaries
		public List<ArchiveSettings> Archives = new List<ArchiveSettings>();

		// OIDC Settings
		public Dictionary<string, string> ProviderToRefreshTokens = new Dictionary<string, string>();

		// Window settings
		public bool bWindowVisible;
		public string WindowState;
		public Rectangle? WindowBounds;
		
		// Schedule settings
		public bool bScheduleEnabled;
		public TimeSpan ScheduleTime;
		public LatestChangeType ScheduleChange;
		public bool ScheduleAnyOpenProject;
		public List<UserSelectedProjectSettings> ScheduleProjects;

		// Run configuration
		public List<Tuple<string, bool>> EditorArguments = new List<Tuple<string,bool>>();
		public bool bEditorArgumentsPrompt;

		// Notification settings
		public List<string> NotifyProjects;
		public int NotifyUnassignedMinutes;
		public int NotifyUnacknowledgedMinutes;
		public int NotifyUnresolvedMinutes;

		// Project settings
		Dictionary<DirectoryReference, UserWorkspaceState> WorkspaceDirToState = new Dictionary<DirectoryReference, UserWorkspaceState>();
		Dictionary<DirectoryReference, UserWorkspaceSettings> WorkspaceDirToSettings = new Dictionary<DirectoryReference, UserWorkspaceSettings>();
		Dictionary<FileReference, UserProjectSettings> ProjectKeyToSettings = new Dictionary<FileReference, UserProjectSettings>();

		// Perforce settings
		public PerforceSyncOptions SyncOptions = new PerforceSyncOptions();

		private List<UserSelectedProjectSettings> ReadProjectList(string SettingName, string LegacySettingName)
		{
			List<UserSelectedProjectSettings> Projects = new List<UserSelectedProjectSettings>();

			string[]? ProjectStrings = ConfigFile.GetValues(SettingName, null);
			if(ProjectStrings != null)
			{
				foreach(string ProjectString in ProjectStrings)
				{
					UserSelectedProjectSettings? Project;
					if(UserSelectedProjectSettings.TryParseConfigEntry(ProjectString, out Project))
					{
						Projects.Add(Project);
					}
				}
			}
			else if(LegacySettingName != null)
			{
				string[]? LegacyProjectStrings = ConfigFile.GetValues(LegacySettingName, null);
				if(LegacyProjectStrings != null)
				{
					foreach(string LegacyProjectString in LegacyProjectStrings)
					{
						if(!String.IsNullOrWhiteSpace(LegacyProjectString))
						{
							Projects.Add(new UserSelectedProjectSettings(null, null, UserSelectedProjectType.Local, null, LegacyProjectString));
						}
					}
				}
			}

			return Projects;
		}

		public UserSettings(FileReference InFileName, ILogger Logger)
		{
			FileName = InFileName;
			ConfigFile.TryLoad(FileName, Logger);

			// General settings
			Version = (UserSettingsVersion)ConfigFile.GetValue("General.Version", (int)UserSettingsVersion.Initial);
			bBuildAfterSync = (ConfigFile.GetValue("General.BuildAfterSync", "1") != "0");
			bRunAfterSync = (ConfigFile.GetValue("General.RunAfterSync", "1") != "0");
			bool bSyncPrecompiledEditor = (ConfigFile.GetValue("General.SyncPrecompiledEditor", "0") != "0");
			bOpenSolutionAfterSync = (ConfigFile.GetValue("General.OpenSolutionAfterSync", "0") != "0");
			bShowLogWindow = (ConfigFile.GetValue("General.ShowLogWindow", false));
			bAutoResolveConflicts = (ConfigFile.GetValue("General.AutoResolveConflicts", "1") != "0");
			bShowUnreviewedChanges = ConfigFile.GetValue("General.ShowUnreviewed", true);
			bShowAutomatedChanges = ConfigFile.GetValue("General.ShowAutomated", false);

			// safely parse the filter enum
			ShowRobomerge = RobomergeShowChangesOption.All;
			Enum.TryParse(ConfigFile.GetValue("General.RobomergeFilter", ""), out ShowRobomerge);

			bAnnotateRobmergeChanges = ConfigFile.GetValue("General.AnnotateRobomerge", true);
			bShowLocalTimes = ConfigFile.GetValue("General.ShowLocalTimes", false);
			bKeepInTray = ConfigFile.GetValue("General.KeepInTray", true);

			List<Guid> EnabledTools = ConfigFile.GetGuidValues("General.EnabledTools", new Guid[0]).ToList();
			if (ConfigFile.GetValue("General.EnableP4VExtensions", false))
			{
				EnabledTools.Add(new Guid("963850A0-BF63-4E0E-B903-1C5954C7DCF8"));
			}
			if (ConfigFile.GetValue("General.EnableUshell", false))
			{
				EnabledTools.Add(new Guid("922EED87-E732-464C-92DC-5A8F7ED955E2"));
			}
			this.EnabledTools = EnabledTools.ToArray();

			bShowNetCoreInfo = ConfigFile.GetValue("General.ShowNetCoreInfo", true);
			int.TryParse(ConfigFile.GetValue("General.FilterIndex", "0"), out FilterIndex);

			string? LastProjectString = ConfigFile.GetValue("General.LastProject", null);
			if(LastProjectString != null)
			{
				UserSelectedProjectSettings.TryParseConfigEntry(LastProjectString, out LastProject);
			}
			else
			{
				string? LastProjectFileName = ConfigFile.GetValue("General.LastProjectFileName", null);
				if(LastProjectFileName != null)
				{
					LastProject = new UserSelectedProjectSettings(null, null, UserSelectedProjectType.Local, null, LastProjectFileName);
				}
			}

			OpenProjects = ReadProjectList("General.OpenProjects", "General.OpenProjectFileNames");
			RecentProjects = ReadProjectList("General.RecentProjects", "General.OtherProjectFileNames");
			SyncView = ConfigFile.GetValues("General.SyncFilter", new string[0]);
			SyncCategories = GetCategorySettings(ConfigFile.FindSection("General"), "SyncIncludedCategories", "SyncExcludedCategories");

			bSyncAllProjects = ConfigFile.GetValue("General.SyncAllProjects", false);
			bIncludeAllProjectsInSolution = ConfigFile.GetValue("General.IncludeAllProjectsInSolution", false);
			if(!Enum.TryParse(ConfigFile.GetValue("General.SyncType", ""), out SyncType))
			{
				SyncType = LatestChangeType.Good;
			}

			// Build configuration
			string CompiledEditorBuildConfigName = ConfigFile.GetValue("General.BuildConfig", "");
			if(!Enum.TryParse(CompiledEditorBuildConfigName, true, out CompiledEditorBuildConfig))
			{
				CompiledEditorBuildConfig = BuildConfig.DebugGame;
			}

			// Tab names
			string TabLabelsValue = ConfigFile.GetValue("General.TabLabels", "");
			if(!Enum.TryParse(TabLabelsValue, true, out TabLabels))
			{
				TabLabels = TabLabels.Stream;
			}

			// Editor arguments
			string[] Arguments = ConfigFile.GetValues("General.EditorArguments", new string[]{ "0:-log", "0:-fastload" });
			if (Version < UserSettingsVersion.XgeShaderCompilation)
			{
				Arguments = Enumerable.Concat(Arguments, new string[] { "0:-noxgeshadercompile" }).ToArray();
			}
			foreach(string Argument in Arguments)
			{
				if(Argument.StartsWith("0:"))
				{
					EditorArguments.Add(new Tuple<string,bool>(Argument.Substring(2), false));
				}
				else if(Argument.StartsWith("1:"))
				{
					EditorArguments.Add(new Tuple<string,bool>(Argument.Substring(2), true));
				}
				else
				{
					EditorArguments.Add(new Tuple<string,bool>(Argument, true));
				}
			}
			bEditorArgumentsPrompt = ConfigFile.GetValue("General.EditorArgumentsPrompt", false);

			// Precompiled binaries
			string[] ArchiveValues = ConfigFile.GetValues("PrecompiledBinaries.Archives", new string[0]);
			foreach (string ArchiveValue in ArchiveValues)
			{
				ArchiveSettings? Settings;
				if (ArchiveSettings.TryParseConfigEntry(ArchiveValue, out Settings))
				{
					Archives.Add(Settings);
				}
			}

			if (bSyncPrecompiledEditor)
			{
				Archives.Add(new ArchiveSettings(true, "Editor", new string[0]));
			}

			// OIDC Settings
			string[] Tokens = ConfigFile.GetValues("OIDCProviders.Tokens", new string[0]);
			foreach (string TokenValue in Tokens)
			{
				ConfigObject O = new ConfigObject(TokenValue);
				string? Provider = O.GetValue("Provider");
				string? Token = O.GetValue("Token");
				if (Provider != null && Token != null)
				{
					ProviderToRefreshTokens.TryAdd(Provider, Token);
				}
			}

			// Window settings
			bWindowVisible = ConfigFile.GetValue("Window.Visible", true);
			WindowState = ConfigFile.GetValue("Window.State", "");
			WindowBounds = ParseRectangleValue(ConfigFile.GetValue("Window.Bounds", ""));

			// Schedule settings
			bScheduleEnabled = ConfigFile.GetValue("Schedule.Enabled", false);
			if(!TimeSpan.TryParse(ConfigFile.GetValue("Schedule.Time", ""), out ScheduleTime))
			{
				ScheduleTime = new TimeSpan(6, 0, 0);
			}
			if(!Enum.TryParse(ConfigFile.GetValue("Schedule.Change", ""), out ScheduleChange))
			{
				ScheduleChange = LatestChangeType.Good;
			}
			ScheduleAnyOpenProject = ConfigFile.GetValue("Schedule.AnyOpenProject", true);
			ScheduleProjects = ReadProjectList("Schedule.Projects", "Schedule.ProjectFileNames");

			// Notification settings
			NotifyProjects = ConfigFile.GetValues("Notifications.NotifyProjects", new string[0]).ToList();
			NotifyUnassignedMinutes = ConfigFile.GetValue("Notifications.NotifyUnassignedMinutes", -1);
			NotifyUnacknowledgedMinutes = ConfigFile.GetValue("Notifications.NotifyUnacknowledgedMinutes", -1);
			NotifyUnresolvedMinutes = ConfigFile.GetValue("Notifications.NotifyUnresolvedMinutes", -1);

			// Perforce settings
			if(!int.TryParse(ConfigFile.GetValue("Perforce.NumRetries", "0"), out SyncOptions.NumRetries))
			{
				SyncOptions.NumRetries = 0;
			}

			int NumThreads;
			if(int.TryParse(ConfigFile.GetValue("Perforce.NumThreads", "0"), out NumThreads) && NumThreads > 0)
			{
				if(Version >= UserSettingsVersion.DefaultNumberOfThreads || NumThreads > 1)
				{
					SyncOptions.NumThreads = NumThreads;
				}
			}

			SyncOptions.TcpBufferSize = ConfigFile.GetValue("Perforce.TcpBufferSize", PerforceSyncOptions.DefaultTcpBufferSize);
			SyncOptions.FileBufferSize = ConfigFile.GetValue("Perforce.FileBufferSize", PerforceSyncOptions.DefaultFileBufferSize);
			SyncOptions.MaxCommandsPerBatch = ConfigFile.GetValue("Perforce.MaxCommandsPerBatch", PerforceSyncOptions.DefaultMaxCommandsPerBatch);
			SyncOptions.MaxSizePerBatch = ConfigFile.GetValue("Perforce.MaxSizePerBatch", PerforceSyncOptions.DefaultMaxSizePerBatch);
		}

		static Dictionary<Guid, bool> GetCategorySettings(ConfigSection Section, string IncludedKey, string ExcludedKey)
		{
			Dictionary<Guid, bool> Result = new Dictionary<Guid, bool>();
			if (Section != null)
			{
				foreach (Guid UniqueId in Section.GetValues(IncludedKey, new Guid[0]))
				{
					Result[UniqueId] = true;
				}
				foreach (Guid UniqueId in Section.GetValues(ExcludedKey, new Guid[0]))
				{
					Result[UniqueId] = false;
				}
			}
			return Result;
		}

		static void SetCategorySettings(ConfigSection Section, string IncludedKey, string ExcludedKey, Dictionary<Guid, bool> Categories)
		{
			Guid[] IncludedCategories = Categories.Where(x => x.Value).Select(x => x.Key).ToArray();
			if (IncludedCategories.Length > 0)
			{
				Section.SetValues(IncludedKey, IncludedCategories);
			}

			Guid[] ExcludedCategories = Categories.Where(x => !x.Value).Select(x => x.Key).ToArray();
			if (ExcludedCategories.Length > 0)
			{
				Section.SetValues(ExcludedKey, ExcludedCategories);
			}
		}

		static Rectangle? ParseRectangleValue(string Text)
		{
			ConfigObject Object = new ConfigObject(Text);

			int X = Object.GetValue("X", -1);
			int Y = Object.GetValue("Y", -1);
			int W = Object.GetValue("W", -1);
			int H = Object.GetValue("H", -1);

			if(X == -1 || Y == -1 || W == -1 || H == -1)
			{
				return null;
			}
			else
			{
				return new Rectangle(X, Y, W, H);
			}
		}

		static string FormatRectangleValue(Rectangle Value)
		{
			ConfigObject Object = new ConfigObject();

			Object.SetValue("X", Value.X);
			Object.SetValue("Y", Value.Y);
			Object.SetValue("W", Value.Width);
			Object.SetValue("H", Value.Height);

			return Object.ToString();
		}

		public static DirectoryReference GetConfigDir(DirectoryReference WorkspaceDir)
		{
			DirectoryReference ConfigDir = DirectoryReference.Combine(WorkspaceDir, ".ugs");

			DirectoryInfo ConfigDirInfo = ConfigDir.ToDirectoryInfo();
			if (!ConfigDirInfo.Exists)
			{
				ConfigDirInfo.Create();
				ConfigDirInfo.Attributes = FileAttributes.Directory | FileAttributes.Hidden;
			}

			return ConfigDir;
		}

		public UserWorkspaceState FindOrAddWorkspaceState(UserWorkspaceSettings Settings)
		{
			UserWorkspaceState? State;
			if (!WorkspaceDirToState.TryGetValue(Settings.RootDir, out State))
			{
				if (!UserWorkspaceState.TryLoad(Settings.RootDir, out State))
				{
					State = ImportWorkspaceState(Settings.RootDir, Settings.ClientName, Settings.BranchPath);
					State.Save();
				}
				WorkspaceDirToState.Add(Settings.RootDir, State);
			}
			if (!String.Equals(Settings.ClientProjectPath, State.CurrentProjectIdentifier))
			{
				State = new UserWorkspaceState();
				State.RootDir = Settings.RootDir;
				State.CurrentProjectIdentifier = Settings.ClientProjectPath;
				WorkspaceDirToState[Settings.RootDir] = State;
			}
			return State;
		}

		public UserWorkspaceSettings FindOrAddWorkspaceSettings(DirectoryReference RootDir, string? ServerAndPort, string? UserName, string ClientName, string BranchPath, string ProjectPath)
		{
			ProjectInfo.ValidateBranchPath(BranchPath);
			ProjectInfo.ValidateProjectPath(ProjectPath);

			UserWorkspaceSettings? Settings;
			if (!WorkspaceDirToSettings.TryGetValue(RootDir, out Settings))
			{
				if (!UserWorkspaceSettings.TryLoad(RootDir, out Settings))
				{
					Settings = ImportWorkspaceSettings(RootDir, ClientName, BranchPath);
				}
				WorkspaceDirToSettings[RootDir] = Settings;
			}

			Settings.ServerAndPort = ServerAndPort;
			Settings.UserName = UserName;
			Settings.ClientName = ClientName;
			Settings.BranchPath = BranchPath;
			Settings.ProjectPath = ProjectPath;
			Settings.Save();

			return Settings;
		}

		UserWorkspaceState ImportWorkspaceState(DirectoryReference RootDir, string ClientName, string BranchPath)
		{
			UserWorkspaceState CurrentWorkspace = new UserWorkspaceState();
			CurrentWorkspace.RootDir = RootDir;

			// Read the workspace settings
			ConfigSection WorkspaceSection = ConfigFile.FindSection(ClientName + BranchPath);
			if(WorkspaceSection == null)
			{
				string LegacyBranchAndClientKey = ClientName + BranchPath;

				int SlashIdx = LegacyBranchAndClientKey.IndexOf('/');
				if(SlashIdx != -1)
				{
					LegacyBranchAndClientKey = LegacyBranchAndClientKey.Substring(0, SlashIdx) + "$" + LegacyBranchAndClientKey.Substring(SlashIdx + 1);
				}

				string? CurrentSync = ConfigFile.GetValue("Clients." + LegacyBranchAndClientKey, null);
				if(CurrentSync != null)
				{
					int AtIdx = CurrentSync.LastIndexOf('@');
					if(AtIdx != -1)
					{
						int ChangeNumber;
						if(int.TryParse(CurrentSync.Substring(AtIdx + 1), out ChangeNumber))
						{
							CurrentWorkspace.CurrentProjectIdentifier = CurrentSync.Substring(0, AtIdx);
							CurrentWorkspace.CurrentChangeNumber = ChangeNumber;
						}
					}
				}

				string? LastUpdateResultText = ConfigFile.GetValue("Clients." + LegacyBranchAndClientKey + "$LastUpdate", null);
				if(LastUpdateResultText != null)
				{
					int ColonIdx = LastUpdateResultText.LastIndexOf(':');
					if(ColonIdx != -1)
					{
						int ChangeNumber;
						if(int.TryParse(LastUpdateResultText.Substring(0, ColonIdx), out ChangeNumber))
						{
							WorkspaceUpdateResult Result;
							if(Enum.TryParse(LastUpdateResultText.Substring(ColonIdx + 1), out Result))
							{
								CurrentWorkspace.LastSyncChangeNumber = ChangeNumber;
								CurrentWorkspace.LastSyncResult = Result;
							}
						}
					}
				}
			}
			else
			{
				CurrentWorkspace.CurrentProjectIdentifier = WorkspaceSection.GetValue("CurrentProjectPath");
				CurrentWorkspace.CurrentChangeNumber = WorkspaceSection.GetValue("CurrentChangeNumber", -1);
				CurrentWorkspace.CurrentSyncFilterHash = WorkspaceSection.GetValue("CurrentSyncFilterHash", null);
				foreach(string AdditionalChangeNumberString in WorkspaceSection.GetValues("AdditionalChangeNumbers", new string[0]))
				{
					int AdditionalChangeNumber;
					if(int.TryParse(AdditionalChangeNumberString, out AdditionalChangeNumber))
					{
						CurrentWorkspace.AdditionalChangeNumbers.Add(AdditionalChangeNumber);
					}
				}

				WorkspaceUpdateResult LastSyncResult;
				Enum.TryParse(WorkspaceSection.GetValue("LastSyncResult", ""), out LastSyncResult);
				CurrentWorkspace.LastSyncResult = LastSyncResult;

				CurrentWorkspace.LastSyncResultMessage = UnescapeText(WorkspaceSection.GetValue("LastSyncResultMessage"));
				CurrentWorkspace.LastSyncChangeNumber = WorkspaceSection.GetValue("LastSyncChangeNumber", -1);

				DateTime LastSyncTime;
				if(DateTime.TryParse(WorkspaceSection.GetValue("LastSyncTime", ""), out LastSyncTime))
				{
					CurrentWorkspace.LastSyncTime = LastSyncTime;
				}

				CurrentWorkspace.LastSyncDurationSeconds = WorkspaceSection.GetValue("LastSyncDuration", 0);
				CurrentWorkspace.LastBuiltChangeNumber = WorkspaceSection.GetValue("LastBuiltChangeNumber", 0);
				CurrentWorkspace.ExpandedArchiveTypes = WorkspaceSection.GetValues("ExpandedArchiveName", new string[0]);

				string[] BisectEntries = WorkspaceSection.GetValues("Bisect", new string[0]);
				foreach(string BisectEntry in BisectEntries)
				{
					ConfigObject BisectEntryObject = new ConfigObject(BisectEntry);

					int ChangeNumber = BisectEntryObject.GetValue("Change", -1);
					if(ChangeNumber != -1)
					{
						BisectState State;
						if(Enum.TryParse(BisectEntryObject.GetValue("State", ""), out State))
						{
							BisectEntry Entry = new BisectEntry();
							Entry.Change = ChangeNumber;
							Entry.State = State;
							CurrentWorkspace.BisectChanges.Add(Entry);
						}
					}
				}
			}

			return CurrentWorkspace;
		}

		UserWorkspaceSettings ImportWorkspaceSettings(DirectoryReference RootDir, string ClientName, string BranchPath)
		{
			UserWorkspaceSettings CurrentWorkspace = new UserWorkspaceSettings();
			CurrentWorkspace.RootDir = RootDir;

			ConfigSection WorkspaceSection = ConfigFile.FindSection(ClientName + BranchPath);
			if (WorkspaceSection == null)
			{
				CurrentWorkspace.SyncView = new string[0];
				CurrentWorkspace.SyncCategories = new List<SyncCategory>();
				CurrentWorkspace.bSyncAllProjects = null;
				CurrentWorkspace.bIncludeAllProjectsInSolution = null;
			}
			else
			{
				CurrentWorkspace.SyncView = WorkspaceSection.GetValues("SyncFilter", new string[0]);
				CurrentWorkspace.SyncCategories = GetCategorySettings(WorkspaceSection, "SyncIncludedCategories", "SyncExcludedCategories").Select(x => new SyncCategory { Id = x.Key, Enable = x.Value }).ToList();

				int SyncAllProjects = WorkspaceSection.GetValue("SyncAllProjects", -1);
				CurrentWorkspace.bSyncAllProjects = (SyncAllProjects == 0) ? (bool?)false : (SyncAllProjects == 1) ? (bool?)true : (bool?)null;

				int IncludeAllProjectsInSolution = WorkspaceSection.GetValue("IncludeAllProjectsInSolution", -1);
				CurrentWorkspace.bIncludeAllProjectsInSolution = (IncludeAllProjectsInSolution == 0) ? (bool?)false : (IncludeAllProjectsInSolution == 1) ? (bool?)true : (bool?)null;
			}

			return CurrentWorkspace;
		}

		public UserProjectSettings FindOrAddProjectSettings(DirectoryReference LocalRoot, string ClientProjectFileName)
		{
			FileReference ConfigFile = FileReference.Combine(GetConfigDir(LocalRoot), $"project_{ClientProjectFileName.Trim('/').Replace('/', '+')}.json");
			if (!ProjectKeyToSettings.TryGetValue(ConfigFile, out UserProjectSettings? Settings))
			{
				if (!Utility.TryLoadJson(ConfigFile, out Settings))
				{
					Settings = ImportProjectSettings(ClientProjectFileName);
					Utility.SaveJson(ConfigFile, Settings);
				}

				Settings.File = ConfigFile;
				ProjectKeyToSettings.Add(ConfigFile, Settings);
			}
			return Settings;
		}

		public UserProjectSettings ImportProjectSettings(string ClientProjectFileName)
		{
			// Read the project settings
			UserProjectSettings CurrentProject = new UserProjectSettings();
	
			ConfigSection ProjectSection = ConfigFile.FindOrAddSection(ClientProjectFileName);
			CurrentProject.BuildSteps.AddRange(ProjectSection.GetValues("BuildStep", new string[0]).Select(x => new ConfigObject(x)));

			FilterType FilterType;
			if (!Enum.TryParse(ProjectSection.GetValue("FilterType", ""), true, out FilterType))
			{
				FilterType = FilterType.None;
			}

			CurrentProject.FilterType = FilterType;
			CurrentProject.FilterBadges.UnionWith(ProjectSection.GetValues("FilterBadges", new string[0]));

			return CurrentProject;
		}

		public void Save()
		{
			// General settings
			ConfigSection GeneralSection = ConfigFile.FindOrAddSection("General");
			GeneralSection.Clear();
			GeneralSection.SetValue("Version", (int)Version);
			GeneralSection.SetValue("BuildAfterSync", bBuildAfterSync);
			GeneralSection.SetValue("RunAfterSync", bRunAfterSync);
			GeneralSection.SetValue("OpenSolutionAfterSync", bOpenSolutionAfterSync);
			GeneralSection.SetValue("ShowLogWindow", bShowLogWindow);
			GeneralSection.SetValue("AutoResolveConflicts", bAutoResolveConflicts);
			GeneralSection.SetValue("ShowUnreviewed", bShowUnreviewedChanges);
			GeneralSection.SetValue("ShowAutomated", bShowAutomatedChanges);
			GeneralSection.SetValue("RobomergeFilter", ShowRobomerge.ToString());
			GeneralSection.SetValue("AnnotateRobomerge", bAnnotateRobmergeChanges);
			GeneralSection.SetValue("ShowLocalTimes", bShowLocalTimes);
			if(LastProject != null)
			{
				GeneralSection.SetValue("LastProject", LastProject.ToConfigEntry());
			}
			GeneralSection.SetValues("OpenProjects", OpenProjects.Select(x => x.ToConfigEntry()).ToArray());
			GeneralSection.SetValue("KeepInTray", bKeepInTray);
			GeneralSection.SetValues("EnabledTools", EnabledTools);
			GeneralSection.SetValue("ShowNetCoreInfo", bShowNetCoreInfo);
			GeneralSection.SetValue("FilterIndex", FilterIndex);
			GeneralSection.SetValues("RecentProjects", RecentProjects.Select(x => x.ToConfigEntry()).ToArray());
			GeneralSection.SetValues("SyncFilter", SyncView);
			SetCategorySettings(GeneralSection, "SyncIncludedCategories", "SyncExcludedCategories", SyncCategories);
			GeneralSection.SetValue("SyncAllProjects", bSyncAllProjects);
			GeneralSection.SetValue("IncludeAllProjectsInSolution", bIncludeAllProjectsInSolution);
			GeneralSection.SetValue("SyncType", SyncType.ToString());

			// Build configuration
			GeneralSection.SetValue("BuildConfig", CompiledEditorBuildConfig.ToString());

			// Tab labels
			GeneralSection.SetValue("TabLabels", TabLabels.ToString());

			// Editor arguments
			List<string> EditorArgumentList = new List<string>();
			foreach(Tuple<string, bool> EditorArgument in EditorArguments)
			{
				EditorArgumentList.Add(String.Format("{0}:{1}", EditorArgument.Item2? 1 : 0, EditorArgument.Item1));
			}
			GeneralSection.SetValues("EditorArguments", EditorArgumentList.ToArray());
			GeneralSection.SetValue("EditorArgumentsPrompt", bEditorArgumentsPrompt);

			// Schedule settings
			ConfigSection ScheduleSection = ConfigFile.FindOrAddSection("Schedule");
			ScheduleSection.Clear();
			ScheduleSection.SetValue("Enabled", bScheduleEnabled);
			ScheduleSection.SetValue("Time", ScheduleTime.ToString());
			ScheduleSection.SetValue("Change", ScheduleChange.ToString());
			ScheduleSection.SetValue("AnyOpenProject", ScheduleAnyOpenProject);
			ScheduleSection.SetValues("Projects", ScheduleProjects.Select(x => x.ToConfigEntry()).ToArray());

			// Precompiled binaries
			ConfigSection ArchivesSection = ConfigFile.FindOrAddSection("PrecompiledBinaries");
			ArchivesSection.SetValues("Archives", Archives.Select(x => x.ToConfigEntry()).ToArray());

			// OIDC Settings
			ConfigSection OIDCSection = ConfigFile.FindOrAddSection("OIDCProviders");
			List<ConfigObject> TokenObjects = new List<ConfigObject>();
			foreach (KeyValuePair<string, string> Pair in ProviderToRefreshTokens)
			{
				ConfigObject TokenEntryObject = new ConfigObject();
				TokenEntryObject.SetValue("Provider", Pair.Key);
				TokenEntryObject.SetValue("Token", Pair.Value);
				TokenObjects.Add(TokenEntryObject);
			}
			OIDCSection.SetValues("Tokens", TokenObjects.Select(x => x.ToString()).ToArray());


			// Window settings
			ConfigSection WindowSection = ConfigFile.FindOrAddSection("Window");
			WindowSection.Clear();
			WindowSection.SetValue("Visible", bWindowVisible);
			WindowSection.SetValue("State", WindowState);
			if(WindowBounds != null)
			{
				WindowSection.SetValue("Bounds", FormatRectangleValue(WindowBounds.Value));
			}

			// Notification settings
			ConfigSection NotificationSection = ConfigFile.FindOrAddSection("Notifications");
			NotificationSection.Clear();
			if (NotifyProjects.Count > 0)
			{
				NotificationSection.SetValues("NotifyProjects", NotifyProjects.ToArray());
			}
			if (NotifyUnassignedMinutes != -1)
			{
				NotificationSection.SetValue("NotifyUnassignedMinutes", NotifyUnassignedMinutes);
			}
			if (NotifyUnacknowledgedMinutes != -1)
			{
				NotificationSection.SetValue("NotifyUnacknowledgedMinutes", NotifyUnacknowledgedMinutes);
			}
			if (NotifyUnresolvedMinutes != -1)
			{
				NotificationSection.SetValue("NotifyUnresolvedMinutes", NotifyUnresolvedMinutes);
			}

			// Perforce settings
			ConfigSection PerforceSection = ConfigFile.FindOrAddSection("Perforce");
			PerforceSection.Clear();
			if(SyncOptions.NumRetries > 0 && SyncOptions.NumRetries != PerforceSyncOptions.DefaultNumRetries)
			{
				PerforceSection.SetValue("NumRetries", SyncOptions.NumRetries);
			}
			if(SyncOptions.NumThreads > 0 && SyncOptions.NumThreads != PerforceSyncOptions.DefaultNumThreads)
			{
				PerforceSection.SetValue("NumThreads", SyncOptions.NumThreads);
			}
			if(SyncOptions.TcpBufferSize > 0 && SyncOptions.TcpBufferSize != PerforceSyncOptions.DefaultTcpBufferSize)
			{
				PerforceSection.SetValue("TcpBufferSize", SyncOptions.TcpBufferSize);
			}
			if (SyncOptions.FileBufferSize > 0 && SyncOptions.FileBufferSize != PerforceSyncOptions.DefaultFileBufferSize)
			{
				PerforceSection.SetValue("FileBufferSize", SyncOptions.FileBufferSize);
			}
			if (SyncOptions.MaxCommandsPerBatch > 0 && SyncOptions.MaxCommandsPerBatch != PerforceSyncOptions.DefaultMaxCommandsPerBatch)
			{
				PerforceSection.SetValue("MaxCommandsPerBatch", SyncOptions.MaxCommandsPerBatch);
			}
			if (SyncOptions.MaxSizePerBatch > 0 && SyncOptions.MaxSizePerBatch != PerforceSyncOptions.DefaultMaxSizePerBatch)
			{
				PerforceSection.SetValue("MaxSizePerBatch", SyncOptions.MaxSizePerBatch);
			}

			// Save the file
			ConfigFile.Save(FileName);
		}

		public static string[] GetCombinedSyncFilter(Dictionary<Guid, WorkspaceSyncCategory> UniqueIdToFilter, string[] GlobalView, Dictionary<Guid, bool> GlobalCategoryIdToSetting, string[] WorkspaceView, Dictionary<Guid, bool> WorkspaceCategoryIdToSetting)
		{
			List<string> Lines = new List<string>();
			foreach(string ViewLine in Enumerable.Concat(GlobalView, WorkspaceView).Select(x => x.Trim()).Where(x => x.Length > 0 && !x.StartsWith(";")))
			{
				Lines.Add(ViewLine);
			}

			HashSet<Guid> Enabled = new HashSet<Guid>();
			foreach (WorkspaceSyncCategory Filter in UniqueIdToFilter.Values)
			{
				bool bEnable = Filter.bEnable;

				bool bGlobalEnable;
				if (GlobalCategoryIdToSetting.TryGetValue(Filter.UniqueId, out bGlobalEnable))
				{
					bEnable = bGlobalEnable;
				}

				bool bWorkspaceEnable;
				if (WorkspaceCategoryIdToSetting.TryGetValue(Filter.UniqueId, out bWorkspaceEnable))
				{
					bEnable = bWorkspaceEnable;
				}

				if(bEnable)
				{
					EnableFilter(Filter.UniqueId, Enabled, UniqueIdToFilter);
				}
			}

			foreach (WorkspaceSyncCategory Filter in UniqueIdToFilter.Values.OrderBy(x => x.Name))
			{
				if (!Enabled.Contains(Filter.UniqueId))
				{
					Lines.AddRange(Filter.Paths.Select(x => "-" + x.Trim()));
				}
			}

			return Lines.ToArray();
		}

		static void EnableFilter(Guid UniqueId, HashSet<Guid> Enabled, Dictionary<Guid, WorkspaceSyncCategory> UniqueIdToFilter)
		{
			if(Enabled.Add(UniqueId))
			{
				WorkspaceSyncCategory? Category;
				if(UniqueIdToFilter.TryGetValue(UniqueId, out Category))
				{
					foreach(Guid RequiresUniqueId in Category.Requires)
					{
						EnableFilter(RequiresUniqueId, Enabled, UniqueIdToFilter);
					}
				}
			}
		}

		[return: NotNullIfNotNull("Text")]
		static string? EscapeText(string? Text)
		{
			if(Text == null)
			{
				return null;
			}

			StringBuilder Result = new StringBuilder();
			for(int Idx = 0; Idx < Text.Length; Idx++)
			{
				switch(Text[Idx])
				{
					case '\\':
						Result.Append("\\\\");
						break;
					case '\t':
						Result.Append("\\t");
						break;
					case '\r':
						Result.Append("\\r");
						break;
					case '\n':
						Result.Append("\\n");
						break;
					case '\'':
						Result.Append("\\\'");
						break;
					case '\"':
						Result.Append("\\\"");
						break;
					default:
						Result.Append(Text[Idx]);
						break;
				}
			}
			return Result.ToString();
		}

		[return: NotNullIfNotNull("Text")]
		static string? UnescapeText(string? Text)
		{
			if(Text == null)
			{
				return null;
			}

			StringBuilder Result = new StringBuilder();
			for(int Idx = 0; Idx < Text.Length; Idx++)
			{
				if(Text[Idx] == '\\' && Idx + 1 < Text.Length)
				{
					switch(Text[++Idx])
					{
						case 't':
							Result.Append('\t');
							break;
						case 'r':
							Result.Append('\r');
							break;
						case 'n':
							Result.Append('\n');
							break;
						case '\'':
							Result.Append('\'');
							break;
						case '\"':
							Result.Append('\"');
							break;
						default:
							Result.Append(Text[Idx]);
							break;
					}
				}
				else
				{
					Result.Append(Text[Idx]);
				}
			}
			return Result.ToString();
		}

		public IEnumerable<FileReference> GetCachedFilePaths()
		{
			List<FileReference> Files = new List<FileReference>();
			Files.AddRange(WorkspaceDirToSettings.Values.Select(x => x.ConfigFile));
			Files.AddRange(WorkspaceDirToState.Values.Select(x => x.ConfigFile));
			Files.AddRange(ProjectKeyToSettings.Keys);
			return Files;
		}
	}
}
