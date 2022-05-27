// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

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

	/// <summary>
	/// Config specified class to determine what is the Latest Change to Sync
	/// Can be configured using badges, good and starred CLs
	/// </summary>
	public class LatestChangeType
	{
		// Display Specifiers
		public string Name = ""; // Name will the saved ID for this LatestChangeType
		public string Description = "";
		public int OrderIndex = Int32.MaxValue;

		// What Rules to Check for.
		public bool bGood = false;
		public bool bStarred = false;
		public bool bFindNewestGoodContent = false;
		public List<string> RequiredBadges = new List<string>();

		public static bool TryParseConfigEntry(string Text, [NotNullWhen(true)] out LatestChangeType? ChangeType)
		{
			ConfigObject DefinitionObject = new ConfigObject(Text);

			string LatestChangeTypeName = DefinitionObject.GetValue("Name", "");
			if (LatestChangeTypeName != "")
			{
				ChangeType = new LatestChangeType();
				ChangeType.Name = LatestChangeTypeName;
				ChangeType.Description = DefinitionObject.GetValue("Description", "No \"Description\" for LatestChangeType given.");
				ChangeType.OrderIndex = DefinitionObject.GetValue("OrderIndex", Int32.MaxValue);

				ChangeType.bGood = DefinitionObject.GetValue("bGood", false);
				ChangeType.bStarred = DefinitionObject.GetValue("bStarred", false);
				ChangeType.bFindNewestGoodContent = DefinitionObject.GetValue("bFindNewestGoodContent", false);
				ChangeType.RequiredBadges = new List<string>(DefinitionObject.GetValue("RequiredBadges", "").Split(new char[] { ',',' '}, StringSplitOptions.RemoveEmptyEntries));
				return true;
			}
			else
			{
				ChangeType = null;
			}

			return false;
		}

		// Default Template for Latest Change
		public static LatestChangeType LatestChange()
		{
			LatestChangeType Latest = new LatestChangeType();
			Latest.Name = "LatestChange";
			Latest.Description = "Latest Change";
			Latest.OrderIndex = -3;
			Latest.bFindNewestGoodContent = true;

			return Latest;
		}

		// Default Template for Latest Good Change
		public static LatestChangeType LatestGoodChange()
		{
			LatestChangeType LatestGood = new LatestChangeType();
			LatestGood.Name = "LatestGoodChange";
			LatestGood.Description = "Latest Good Change";
			LatestGood.OrderIndex = -2;
			LatestGood.bGood = true;
			LatestGood.bFindNewestGoodContent = true;

			return LatestGood;
		}

		// Default Template for Latest Starred Change
		public static LatestChangeType LatestStarredChange()
		{
			LatestChangeType LatestStarred = new LatestChangeType();
			LatestStarred.Name = "LatestStarredChange";
			LatestStarred.Description = "Latest Starred Change";
			LatestStarred.OrderIndex = -1;
			LatestStarred.bStarred = true;
			LatestStarred.bFindNewestGoodContent = true;

			return LatestStarred;
		}
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
		public string? ServerAndPort { get; set; }
		public string? UserName { get; set; }
		public UserSelectedProjectType Type { get; set; }
		public string? ClientPath { get; set; }
		public string? LocalPath { get; set; }
		public string? ScheduledSyncTypeID { get; set; }

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

				string? ScheduledSyncTypeID = Object.GetValue("ScheduledSyncTypeID", null);
				if (String.IsNullOrWhiteSpace(ScheduledSyncTypeID))
				{
					ScheduledSyncTypeID = null;
				}

				if ((Type == UserSelectedProjectType.Client && ClientPath != null) || (Type == UserSelectedProjectType.Local && LocalPath != null))
				{
					Project = new UserSelectedProjectSettings(ServerAndPort, UserName, Type, ClientPath, LocalPath);
					Project.ScheduledSyncTypeID = ScheduledSyncTypeID;
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
			if (ScheduledSyncTypeID != null)
			{
				Object.SetValue("ScheduledSyncTypeID", ScheduledSyncTypeID);
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

		[JsonIgnore]
		public long LastModifiedTimeUtc { get; set; }

		// Connection settings
		public string? ServerAndPort { get; set; }
		public string? UserName { get; set; }
		public string ClientName { get; set; } = String.Empty;

		// Path to the root of the branch within this client, with a trailing slash if non-empty
		public string BranchPath { get; set; } = String.Empty;

		// The currently selected project, relative to the root directory
		public string ProjectPath { get; set; } = String.Empty;

		// Workspace specific SyncFilters
		public FilterSettings Filter { get; set; } = new FilterSettings();

		[JsonIgnore]
		public FileReference ConfigFile => GetConfigFile(RootDir);

		[JsonIgnore]
		public string ClientProjectPath => $"//{ClientName}{BranchPath}{ProjectPath}";

		[JsonIgnore]
		public FileReference LocalProjectPath => new FileReference(RootDir.FullName + ProjectPath);

		public void Init(string? ServerAndPort, string? UserName, string ClientName, string BranchPath, string ProjectPath)
		{
			ProjectInfo.ValidateBranchPath(BranchPath);
			ProjectInfo.ValidateProjectPath(ProjectPath);

			this.ServerAndPort = ServerAndPort;
			this.UserName = UserName;
			this.ClientName = ClientName;
			this.BranchPath = BranchPath;
			this.ProjectPath = ProjectPath;
		}

		public static bool TryLoad(DirectoryReference RootDir, [NotNullWhen(true)] out UserWorkspaceSettings? Settings)
		{
			FileReference ConfigFile = GetConfigFile(RootDir);
			if (Utility.TryLoadJson(ConfigFile, out Settings))
			{
				Settings.RootDir = RootDir;
				Settings.LastModifiedTimeUtc = FileReference.GetLastWriteTimeUtc(ConfigFile).Ticks;
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
				LastModifiedTimeUtc = FileReference.GetLastWriteTimeUtc(ConfigFile).Ticks;
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

		// Cached state about the project, configured using UserWorkspaceSettings and taken from computed values in ProjectInfo. Assumed valid unless manually updated.
		public long SettingsTimeUtc { get; set; }
		public string ClientName { get; set; } = String.Empty;
		public string BranchPath { get; set; } = String.Empty;
		public string ProjectPath { get; set; } = String.Empty;
		public string? StreamName { get; set; }
		public string ProjectIdentifier { get; set; } = String.Empty; // Should be fully reset if this changes
		public bool IsEnterpriseProject { get; set; }

		// Settings for the currently synced project in this workspace. CurrentChangeNumber is only valid for this workspace if CurrentProjectPath is the current project.
		public int CurrentChangeNumber { get; set; } = -1;
		public int CurrentCodeChangeNumber { get; set; } = -1;
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
		[JsonIgnore]
		public FileReference ConfigFile => GetConfigFile(RootDir);

		public static UserWorkspaceState CreateNew(DirectoryReference RootDir)
		{
			UserWorkspaceState State = new UserWorkspaceState();
			State.RootDir = RootDir;
			return State;
		}

		public ProjectInfo CreateProjectInfo()
		{
			return new ProjectInfo(RootDir, ClientName, BranchPath, ProjectPath, StreamName, ProjectIdentifier, IsEnterpriseProject);
		}

		public void UpdateCachedProjectInfo(ProjectInfo ProjectInfo, long SettingsTimeUtc)
		{
			this.SettingsTimeUtc = SettingsTimeUtc;

			RootDir = ProjectInfo.LocalRootPath;
			ClientName = ProjectInfo.ClientName;
			BranchPath = ProjectInfo.BranchPath;
			ProjectPath = ProjectInfo.ProjectPath;
			StreamName = ProjectInfo.StreamName;
			ProjectIdentifier = ProjectInfo.ProjectIdentifier;
			IsEnterpriseProject = ProjectInfo.bIsEnterpriseProject;
		}

		public bool IsValid(ProjectInfo ProjectInfo)
		{
			return ProjectIdentifier.Equals(ProjectInfo.ProjectIdentifier, StringComparison.OrdinalIgnoreCase);
		}

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
		public FileReference ConfigFile { get; private set; } = null!;

		public List<ConfigObject> BuildSteps { get; set; } = new List<ConfigObject>();
		public FilterType FilterType { get; set; }
		public HashSet<string> FilterBadges { get; set; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

		static object SyncRoot = new object();

		private UserProjectSettings()
		{
		}

		public UserProjectSettings(FileReference ConfigFile)
		{
			this.ConfigFile = ConfigFile;
		}

		public static bool TryLoad(FileReference ConfigFile, [NotNullWhen(true)] out UserProjectSettings? Settings)
		{
			if (FileReference.Exists(ConfigFile))
			{
				Settings = Utility.LoadJson<UserProjectSettings>(ConfigFile);
				Settings.ConfigFile = ConfigFile;
				return true;
			}

			Settings = null;
			return false;
		}

		public void Save()
		{
			lock (SyncRoot)
			{
				Utility.SaveJson(ConfigFile, this);
			}
		}
	}

	public class UserSettings : GlobalSettingsFile
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
		ConfigFile ConfigFile;

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
		public int FilterIndex;
		public UserSelectedProjectSettings? LastProject;
		public List<UserSelectedProjectSettings> OpenProjects;
		public List<UserSelectedProjectSettings> RecentProjects;
		public string SyncTypeID;
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

		public static UserSettings Create(DirectoryReference SettingsDir, ILogger Logger)
		{
			return Create(FileReference.Combine(SettingsDir, "UnrealGameSyncV2.ini"), FileReference.Combine(SettingsDir, "UnrealGameSync.ini"), FileReference.Combine(SettingsDir, "Global.json"), Logger);
		}

		public static UserSettings Create(FileReference FileName, FileReference LegacyFileName, FileReference CoreFileName, ILogger Logger)
		{
			ConfigFile ConfigFile = new ConfigFile();
			if (FileReference.Exists(FileName))
			{
				ConfigFile.TryLoad(FileName, Logger);
			}
			else
			{
				ConfigFile.TryLoad(LegacyFileName, Logger);
			}

			GlobalSettings? CoreSettingsData;
			if(FileReference.Exists(CoreFileName))
			{
				CoreSettingsData = Utility.LoadJson<GlobalSettings>(CoreFileName);
			}
			else
			{
				CoreSettingsData = new GlobalSettings();
				CoreSettingsData.Filter.View = ConfigFile.GetValues("General.SyncFilter", new string[0]).ToList();
				CoreSettingsData.Filter.SetCategories(GetCategorySettings(ConfigFile.FindSection("General"), "SyncIncludedCategories", "SyncExcludedCategories"));
				CoreSettingsData.Filter.AllProjects = ConfigFile.GetValue("General.SyncAllProjects", false);
				CoreSettingsData.Filter.AllProjectsInSln = ConfigFile.GetValue("General.IncludeAllProjectsInSolution", false);
			}

			return new UserSettings(FileName, ConfigFile, CoreFileName, CoreSettingsData);
		}

		public UserSettings(FileReference InFileName, ConfigFile InConfigFile, FileReference InCoreFileName, GlobalSettings InCoreSettingsData)
			: base(InCoreFileName, InCoreSettingsData)
		{
			this.FileName = InFileName;
			this.ConfigFile = InConfigFile;

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
			SyncTypeID = ConfigFile.GetValue("General.SyncTypeID", "");
			string? OldSyncTye = ConfigFile.GetValue("General.SyncType", null);
			if (OldSyncTye != null)
			{
				if (OldSyncTye == "Any")
				{
					SyncTypeID = LatestChangeType.LatestChange().Name;
				}
				else if (OldSyncTye == "Good")
				{
					SyncTypeID = LatestChangeType.LatestGoodChange().Name;
				}
				else if (OldSyncTye == "Starred")
				{
					SyncTypeID = LatestChangeType.LatestStarredChange().Name;
				}
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
			SyncOptions.NumSyncErrorRetries = ConfigFile.GetValue("Perforce.NumSyncErrorRetries", PerforceSyncOptions.DefaultNumSyncErrorRetries);
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
				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					ConfigDirInfo.Attributes = FileAttributes.Directory | FileAttributes.Hidden;
				}
			}

			return ConfigDir;
		}

		protected override void ImportWorkspaceState(DirectoryReference RootDir, string ClientName, string BranchPath, UserWorkspaceState CurrentWorkspace)
		{
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
							CurrentWorkspace.ProjectIdentifier = CurrentSync.Substring(0, AtIdx);
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
				CurrentWorkspace.ProjectIdentifier = WorkspaceSection.GetValue("CurrentProjectPath", "");
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
		}

		protected override void ImportWorkspaceSettings(DirectoryReference RootDir, string ClientName, string BranchPath, UserWorkspaceSettings CurrentWorkspace)
		{
			ConfigSection WorkspaceSection = ConfigFile.FindSection(ClientName + BranchPath);
			if (WorkspaceSection != null)
			{
				CurrentWorkspace.Filter.View = WorkspaceSection.GetValues("SyncFilter", new string[0]).ToList();
				CurrentWorkspace.Filter.SetCategories(GetCategorySettings(WorkspaceSection, "SyncIncludedCategories", "SyncExcludedCategories"));

				int SyncAllProjects = WorkspaceSection.GetValue("SyncAllProjects", -1);
				CurrentWorkspace.Filter.AllProjects = (SyncAllProjects == 0) ? (bool?)false : (SyncAllProjects == 1) ? (bool?)true : (bool?)null;

				int IncludeAllProjectsInSolution = WorkspaceSection.GetValue("IncludeAllProjectsInSolution", -1);
				CurrentWorkspace.Filter.AllProjectsInSln = (IncludeAllProjectsInSolution == 0) ? (bool?)false : (IncludeAllProjectsInSolution == 1) ? (bool?)true : (bool?)null;
			}
		}

		protected override void ImportProjectSettings(ProjectInfo ProjectInfo, UserProjectSettings CurrentProject)
		{
			string ClientProjectFileName = ProjectInfo.ClientFileName;

			// Read the project settings
			ConfigSection ProjectSection = ConfigFile.FindOrAddSection(ClientProjectFileName);
			CurrentProject.BuildSteps.AddRange(ProjectSection.GetValues("BuildStep", new string[0]).Select(x => new ConfigObject(x)));

			FilterType FilterType;
			if (!Enum.TryParse(ProjectSection.GetValue("FilterType", ""), true, out FilterType))
			{
				FilterType = FilterType.None;
			}

			CurrentProject.FilterType = FilterType;
			CurrentProject.FilterBadges.UnionWith(ProjectSection.GetValues("FilterBadges", new string[0]));
		}

		public override void Save()
		{
			base.Save();

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
			GeneralSection.SetValue("FilterIndex", FilterIndex);
			GeneralSection.SetValues("RecentProjects", RecentProjects.Select(x => x.ToConfigEntry()).ToArray());
			GeneralSection.SetValue("SyncTypeID", SyncTypeID);

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
			if (SyncOptions.NumSyncErrorRetries > 0 && SyncOptions.NumSyncErrorRetries != PerforceSyncOptions.DefaultNumSyncErrorRetries)
			{
				PerforceSection.SetValue("NumSyncErrorRetries", SyncOptions.NumSyncErrorRetries);
			}

			// Save the file
			ConfigFile.Save(FileName);
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
