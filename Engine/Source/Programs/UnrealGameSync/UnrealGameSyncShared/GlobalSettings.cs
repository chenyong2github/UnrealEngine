// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	public class FilterSettings
	{
		public List<Guid> IncludeCategories { get; set; } = new List<Guid>();
		public List<Guid> ExcludeCategories { get; set; } = new List<Guid>();
		public List<string> View { get; set; } = new List<string>();
		public bool? AllProjects { get; set; }
		public bool? AllProjectsInSln { get; set; }

		public void Reset()
		{
			IncludeCategories.Clear();
			ExcludeCategories.Clear();
			View.Clear();
			AllProjects = null;
			AllProjectsInSln = null;
		}

		public void SetCategories(Dictionary<Guid, bool> Categories)
		{
			IncludeCategories = Categories.Where(x => x.Value).Select(x => x.Key).ToList();
			ExcludeCategories = Categories.Where(x => !x.Value).Select(x => x.Key).ToList();
		}

		public Dictionary<Guid, bool> GetCategories()
		{
			Dictionary<Guid, bool> Categories = new Dictionary<Guid, bool>();
			foreach (Guid IncludeCategory in IncludeCategories)
			{
				Categories[IncludeCategory] = true;
			}
			foreach (Guid ExcludeCategory in ExcludeCategories)
			{
				Categories[ExcludeCategory] = false;
			}
			return Categories;
		}
	}

	public class GlobalSettings
	{
		public FilterSettings Filter { get; set; } = new FilterSettings();
	}

	public class GlobalSettingsFile
	{
		public FileReference File { get; }
		public GlobalSettings Global { get; }

		public UserProjectSettings FindOrAddProjectSettings(ProjectInfo ProjectInfo, UserWorkspaceSettings Settings)
		{
			FileReference ConfigFile;
			if (ProjectInfo.LocalFileName.HasExtension(".uprojectdirs"))
			{
				ConfigFile = FileReference.Combine(UserSettings.GetConfigDir(Settings.RootDir), "project.json");
			}
			else
			{
				ConfigFile = FileReference.Combine(UserSettings.GetConfigDir(Settings.RootDir), $"project_{ProjectInfo.LocalFileName.GetFileNameWithoutExtension()}.json");
			}

			UserSettings.CreateConfigDir(ConfigFile.Directory);

			UserProjectSettings? ProjectSettings;
			if (!UserProjectSettings.TryLoad(ConfigFile, out ProjectSettings))
			{
				ProjectSettings = new UserProjectSettings(ConfigFile);
				ImportProjectSettings(ProjectInfo, ProjectSettings);
				ProjectSettings.Save();
			}
			return ProjectSettings;
		}

		protected virtual void ImportProjectSettings(ProjectInfo ProjectInfo, UserProjectSettings ProjectSettings)
		{
		}

		protected virtual void ImportWorkspaceSettings(DirectoryReference RootDir, string ClientName, string BranchPath, UserWorkspaceSettings WorkspaceSettings)
		{
		}

		protected virtual void ImportWorkspaceState(DirectoryReference RootDir, string ClientName, string BranchPath, UserWorkspaceState WorkspaceState)
		{
		}

		public GlobalSettingsFile(FileReference File, GlobalSettings Global)
		{
			this.File = File;
			this.Global = Global;
		}

		public static GlobalSettingsFile Create(FileReference File)
		{
			GlobalSettings? Data;
			if (!Utility.TryLoadJson(File, out Data))
			{
				Data = new GlobalSettings();
			}
			return new GlobalSettingsFile(File, Data);
		}

		public virtual void Save()
		{
			Utility.SaveJson(File, Global);
		}

		public UserWorkspaceState FindOrAddWorkspaceState(UserWorkspaceSettings Settings)
		{
			return FindOrAddWorkspaceState(Settings.RootDir, Settings.ClientName, Settings.BranchPath);
		}

		public UserWorkspaceState FindOrAddWorkspaceState(DirectoryReference RootDir, string ClientName, string BranchPath)
		{
			UserWorkspaceState? State;
			if (!UserWorkspaceState.TryLoad(RootDir, out State))
			{
				State = new UserWorkspaceState();
				State.RootDir = RootDir;
				ImportWorkspaceState(RootDir, ClientName, BranchPath, State);
				State.Save();
			}
			return State;
		}

		public UserWorkspaceState FindOrAddWorkspaceState(ProjectInfo ProjectInfo, UserWorkspaceSettings Settings)
		{
			UserWorkspaceState State = FindOrAddWorkspaceState(ProjectInfo.LocalRootPath, ProjectInfo.ClientName, ProjectInfo.BranchPath);
			if (!State.IsValid(ProjectInfo))
			{
				State = new UserWorkspaceState();
			}
			State.UpdateCachedProjectInfo(ProjectInfo, Settings.LastModifiedTimeUtc);
			return State;
		}

		public UserWorkspaceSettings FindOrAddWorkspaceSettings(DirectoryReference RootDir, string? ServerAndPort, string? UserName, string ClientName, string BranchPath, string ProjectPath)
		{
			ProjectInfo.ValidateBranchPath(BranchPath);
			ProjectInfo.ValidateProjectPath(ProjectPath);

			UserWorkspaceSettings? Settings;
			if (!UserWorkspaceSettings.TryLoad(RootDir, out Settings))
			{
				Settings = new UserWorkspaceSettings();
				Settings.RootDir = RootDir;
				ImportWorkspaceSettings(RootDir, ClientName, BranchPath, Settings);
			}

			Settings.Init(ServerAndPort, UserName, ClientName, BranchPath, ProjectPath);
			Settings.Save();

			return Settings;
		}

		public static string[] GetCombinedSyncFilter(Dictionary<Guid, WorkspaceSyncCategory> UniqueIdToFilter, FilterSettings GlobalFilter, FilterSettings WorkspaceFilter)
		{
			List<string> Lines = new List<string>();
			foreach (string ViewLine in Enumerable.Concat(GlobalFilter.View, WorkspaceFilter.View).Select(x => x.Trim()).Where(x => x.Length > 0 && !x.StartsWith(";")))
			{
				Lines.Add(ViewLine);
			}

			Dictionary<Guid, bool> GlobalCategoryIdToSetting = GlobalFilter.GetCategories();
			Dictionary<Guid, bool> WorkspaceCategoryIdToSetting = WorkspaceFilter.GetCategories();

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

				if (bEnable)
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
			if (Enabled.Add(UniqueId))
			{
				WorkspaceSyncCategory? Category;
				if (UniqueIdToFilter.TryGetValue(UniqueId, out Category))
				{
					foreach (Guid RequiresUniqueId in Category.Requires)
					{
						EnableFilter(RequiresUniqueId, Enabled, UniqueIdToFilter);
					}
				}
			}
		}
	}
}
