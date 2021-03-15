// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	partial class SyncFilter : Form
	{
		Dictionary<Guid, WorkspaceSyncCategory> UniqueIdToCategory;
		public string[] GlobalView;
		public Dictionary<Guid, bool> GlobalSyncCategories;
		public bool bGlobalSyncAllProjects;
		public bool bGlobalIncludeAllProjectsInSolution;
		public string[] WorkspaceView;
		public Dictionary<Guid, bool> WorkspaceSyncCategories;
		public bool? bWorkspaceSyncAllProjects;
		public bool? bWorkspaceIncludeAllProjectsInSolution;

		public SyncFilter(Dictionary<Guid, WorkspaceSyncCategory> InUniqueIdToCategory, string[] InGlobalView, Dictionary<Guid, bool> InGlobalSyncCategories, bool bInGlobalProjectOnly, bool bInGlobalIncludeAllProjectsInSolution, string[] InWorkspaceView, Dictionary<Guid, bool> InWorkspaceSyncCategories, bool? bInWorkspaceProjectOnly, bool? bInWorkspaceIncludeAllProjectsInSolution)
		{
			InitializeComponent();

			UniqueIdToCategory = InUniqueIdToCategory;
			GlobalSyncCategories = InGlobalSyncCategories;
			GlobalView = InGlobalView;
			bGlobalSyncAllProjects = bInGlobalProjectOnly;
			bGlobalIncludeAllProjectsInSolution = bInGlobalIncludeAllProjectsInSolution;
			WorkspaceSyncCategories = InWorkspaceSyncCategories;
			WorkspaceView = InWorkspaceView;
			bWorkspaceSyncAllProjects = bInWorkspaceProjectOnly;
			bWorkspaceIncludeAllProjectsInSolution = bInWorkspaceIncludeAllProjectsInSolution;

			Dictionary<Guid, bool> SyncCategories = WorkspaceSyncCategory.GetDefault(UniqueIdToCategory.Values);

			WorkspaceSyncCategory.ApplyDelta(SyncCategories, InGlobalSyncCategories);
			GlobalControl.SetView(GlobalView);
			SetExcludedCategories(GlobalControl.CategoriesCheckList, UniqueIdToCategory, SyncCategories);
			GlobalControl.SyncAllProjects.Checked = bGlobalSyncAllProjects;
			GlobalControl.IncludeAllProjectsInSolution.Checked = bGlobalIncludeAllProjectsInSolution;

			WorkspaceSyncCategory.ApplyDelta(SyncCategories, InWorkspaceSyncCategories);
			WorkspaceControl.SetView(WorkspaceView);
			SetExcludedCategories(WorkspaceControl.CategoriesCheckList, UniqueIdToCategory, SyncCategories);
			WorkspaceControl.SyncAllProjects.Checked = bWorkspaceSyncAllProjects ?? bGlobalSyncAllProjects;
			WorkspaceControl.IncludeAllProjectsInSolution.Checked = bWorkspaceIncludeAllProjectsInSolution ?? bGlobalIncludeAllProjectsInSolution;

			GlobalControl.CategoriesCheckList.ItemCheck += GlobalControl_CategoriesCheckList_ItemCheck;
			GlobalControl.SyncAllProjects.CheckStateChanged += GlobalControl_SyncAllProjects_CheckStateChanged;
			GlobalControl.IncludeAllProjectsInSolution.CheckStateChanged += GlobalControl_IncludeAllProjectsInSolution_CheckStateChanged;
		}

		private void GlobalControl_CategoriesCheckList_ItemCheck(object sender, ItemCheckEventArgs e)
		{
			WorkspaceControl.CategoriesCheckList.SetItemCheckState(e.Index, e.NewValue);
		}

		private void GlobalControl_SyncAllProjects_CheckStateChanged(object sender, EventArgs e)
		{
			WorkspaceControl.SyncAllProjects.Checked = GlobalControl.SyncAllProjects.Checked;
		}

		private void GlobalControl_IncludeAllProjectsInSolution_CheckStateChanged(object sender, EventArgs e)
		{
			WorkspaceControl.IncludeAllProjectsInSolution.Checked = GlobalControl.IncludeAllProjectsInSolution.Checked;
		}

		private static void SetExcludedCategories(CheckedListBox ListBox, Dictionary<Guid, WorkspaceSyncCategory> UniqueIdToFilter, Dictionary<Guid, bool> CategoryIdToSetting)
		{
			ListBox.Items.Clear();
			foreach(WorkspaceSyncCategory Filter in UniqueIdToFilter.Values)
			{
				if (!Filter.bHidden)
				{
					CheckState State = CheckState.Checked;
					if (!CategoryIdToSetting[Filter.UniqueId])
					{
						State = CheckState.Unchecked;
					}
					ListBox.Items.Add(Filter, State);
				}
			}
		}

		private void GetExcludedCategories(out Dictionary<Guid, bool> NewGlobalSyncCategories, out Dictionary<Guid, bool> NewWorkspaceSyncCategories)
		{
			Dictionary<Guid, bool> DefaultSyncCategories = WorkspaceSyncCategory.GetDefault(UniqueIdToCategory.Values);

			Dictionary<Guid, bool> GlobalSyncCategories = GetCategorySettings(GlobalControl.CategoriesCheckList, this.GlobalSyncCategories);
			NewGlobalSyncCategories = WorkspaceSyncCategory.GetDelta(DefaultSyncCategories, GlobalSyncCategories);

			Dictionary<Guid, bool> WorkspaceSyncCategories = GetCategorySettings(WorkspaceControl.CategoriesCheckList, this.WorkspaceSyncCategories);
			NewWorkspaceSyncCategories = WorkspaceSyncCategory.GetDelta(GlobalSyncCategories, WorkspaceSyncCategories);
		}

		private Dictionary<Guid, bool> GetCategorySettings(CheckedListBox ListBox, IEnumerable<KeyValuePair<Guid, bool>> OriginalSettings)
		{
			Dictionary<Guid, bool> Result = new Dictionary<Guid, bool>();
			for(int Idx = 0; Idx < ListBox.Items.Count; Idx++)
			{
				Guid UniqueId = ((WorkspaceSyncCategory)ListBox.Items[Idx]).UniqueId;
				if (!Result.ContainsKey(UniqueId))
				{
					Result[UniqueId] = ListBox.GetItemCheckState(Idx) == CheckState.Checked;
				}
			}
			foreach (KeyValuePair<Guid, bool> OriginalSetting in OriginalSettings)
			{
				if (!UniqueIdToCategory.ContainsKey(OriginalSetting.Key))
				{
					Result[OriginalSetting.Key] = OriginalSetting.Value;
				}
			}
			return Result;
		}

		private static string[] GetView(TextBox FilterText)
		{
			List<string> NewLines = new List<string>(FilterText.Lines);
			while (NewLines.Count > 0 && NewLines.Last().Trim().Length == 0)
			{
				NewLines.RemoveAt(NewLines.Count - 1);
			}
			return NewLines.Count > 0 ? FilterText.Lines : new string[0];
		}

		private void OkButton_Click(object sender, EventArgs e)
		{
			string[] NewGlobalView = GlobalControl.GetView();
			string[] NewWorkspaceView = WorkspaceControl.GetView();

			if(NewGlobalView.Any(x => x.Contains("//")) || NewWorkspaceView.Any(x => x.Contains("//")))
			{
				if(MessageBox.Show(this, "Custom views should be relative to the stream root (eg. -/Engine/...).\r\n\r\nFull depot paths (eg. //depot/...) will not match any files.\r\n\r\nAre you sure you want to continue?", "Invalid view", MessageBoxButtons.OKCancel) != System.Windows.Forms.DialogResult.OK)
				{
					return;
				}
			}
		
			GlobalView = NewGlobalView;
			bGlobalSyncAllProjects = GlobalControl.SyncAllProjects.Checked;
			bGlobalIncludeAllProjectsInSolution = GlobalControl.IncludeAllProjectsInSolution.Checked;

			WorkspaceView = NewWorkspaceView;
			bWorkspaceSyncAllProjects = (WorkspaceControl.SyncAllProjects.Checked == bGlobalSyncAllProjects)? (bool?)null : WorkspaceControl.SyncAllProjects.Checked;
			bWorkspaceIncludeAllProjectsInSolution = (WorkspaceControl.IncludeAllProjectsInSolution.Checked == bGlobalIncludeAllProjectsInSolution)? (bool?)null : WorkspaceControl.IncludeAllProjectsInSolution.Checked;

			GetExcludedCategories(out GlobalSyncCategories, out WorkspaceSyncCategories);

			DialogResult = DialogResult.OK;
		}

		private void CancButton_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
		}

		private void ShowCombinedView_Click(object sender, EventArgs e)
		{
			Dictionary<Guid, bool> NewGlobalSyncCategories;
			Dictionary<Guid, bool> NewWorkspaceSyncCategories;
			GetExcludedCategories(out NewGlobalSyncCategories, out NewWorkspaceSyncCategories);

			string[] Filter = UserSettings.GetCombinedSyncFilter(UniqueIdToCategory, GlobalControl.GetView(), NewGlobalSyncCategories, WorkspaceControl.GetView(), NewWorkspaceSyncCategories);
			if(Filter.Length == 0)
			{
				Filter = new string[]{ "All files will be synced." };
			}
			MessageBox.Show(String.Join("\r\n", Filter), "Combined View");
		}
	}
}
