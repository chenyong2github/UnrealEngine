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
		public FilterSettings GlobalFilter;
		public FilterSettings WorkspaceFilter;

		public SyncFilter(Dictionary<Guid, WorkspaceSyncCategory> InUniqueIdToCategory, FilterSettings InGlobalFilter, FilterSettings InWorkspaceFilter)
		{
			InitializeComponent();

			UniqueIdToCategory = InUniqueIdToCategory;
			GlobalFilter = InGlobalFilter;
			WorkspaceFilter = InWorkspaceFilter;

			Dictionary<Guid, bool> SyncCategories = WorkspaceSyncCategory.GetDefault(UniqueIdToCategory.Values);

			WorkspaceSyncCategory.ApplyDelta(SyncCategories, GlobalFilter.GetCategories());
			GlobalControl.SetView(GlobalFilter.View.ToArray());
			SetExcludedCategories(GlobalControl.CategoriesCheckList, UniqueIdToCategory, SyncCategories);
			GlobalControl.SyncAllProjects.Checked = GlobalFilter.AllProjects ?? false;
			GlobalControl.IncludeAllProjectsInSolution.Checked = GlobalFilter.AllProjectsInSln ?? false;

			WorkspaceSyncCategory.ApplyDelta(SyncCategories, WorkspaceFilter.GetCategories());
			WorkspaceControl.SetView(WorkspaceFilter.View.ToArray());
			SetExcludedCategories(WorkspaceControl.CategoriesCheckList, UniqueIdToCategory, SyncCategories);
			WorkspaceControl.SyncAllProjects.Checked = WorkspaceFilter.AllProjects ?? GlobalFilter.AllProjects ?? false;
			WorkspaceControl.IncludeAllProjectsInSolution.Checked = WorkspaceFilter.AllProjectsInSln ?? GlobalFilter.AllProjectsInSln ?? false;

			GlobalControl.CategoriesCheckList.ItemCheck += GlobalControl_CategoriesCheckList_ItemCheck;
			GlobalControl.SyncAllProjects.CheckStateChanged += GlobalControl_SyncAllProjects_CheckStateChanged;
			GlobalControl.IncludeAllProjectsInSolution.CheckStateChanged += GlobalControl_IncludeAllProjectsInSolution_CheckStateChanged;
		}

		private void GlobalControl_CategoriesCheckList_ItemCheck(object? sender, ItemCheckEventArgs e)
		{
			WorkspaceControl.CategoriesCheckList.SetItemCheckState(e.Index, e.NewValue);
		}

		private void GlobalControl_SyncAllProjects_CheckStateChanged(object? sender, EventArgs e)
		{
			WorkspaceControl.SyncAllProjects.Checked = GlobalControl.SyncAllProjects.Checked;
		}

		private void GlobalControl_IncludeAllProjectsInSolution_CheckStateChanged(object? sender, EventArgs e)
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

		private void GetSettings(out FilterSettings NewGlobalFilter, out FilterSettings NewWorkspaceFilter)
		{
			Dictionary<Guid, bool> DefaultSyncCategories = WorkspaceSyncCategory.GetDefault(UniqueIdToCategory.Values);

			NewGlobalFilter = new FilterSettings();
			NewGlobalFilter.View = GlobalControl.GetView().ToList();
			NewGlobalFilter.AllProjects = GlobalControl.SyncAllProjects.Checked;
			NewGlobalFilter.AllProjectsInSln = GlobalControl.IncludeAllProjectsInSolution.Checked;

			Dictionary<Guid, bool> GlobalSyncCategories = GetCategorySettings(GlobalControl.CategoriesCheckList, GlobalFilter.GetCategories());
			NewGlobalFilter.SetCategories(WorkspaceSyncCategory.GetDelta(DefaultSyncCategories, GlobalSyncCategories));

			NewWorkspaceFilter = new FilterSettings();
			NewWorkspaceFilter.View = WorkspaceControl.GetView().ToList();
			NewWorkspaceFilter.AllProjects = (WorkspaceControl.SyncAllProjects.Checked == NewGlobalFilter.AllProjects) ? (bool?)null : WorkspaceControl.SyncAllProjects.Checked;
			NewWorkspaceFilter.AllProjectsInSln = (WorkspaceControl.IncludeAllProjectsInSolution.Checked == NewGlobalFilter.AllProjectsInSln) ? (bool?)null : WorkspaceControl.IncludeAllProjectsInSolution.Checked;

			Dictionary<Guid, bool> WorkspaceSyncCategories = GetCategorySettings(WorkspaceControl.CategoriesCheckList, WorkspaceFilter.GetCategories());
			NewWorkspaceFilter.SetCategories(WorkspaceSyncCategory.GetDelta(GlobalSyncCategories, WorkspaceSyncCategories));
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
			GetSettings(out FilterSettings NewGlobalFilter, out FilterSettings NewWorkspaceFilter);

			if(NewGlobalFilter.View.Any(x => x.Contains("//")) || NewWorkspaceFilter.View.Any(x => x.Contains("//")))
			{
				if(MessageBox.Show(this, "Custom views should be relative to the stream root (eg. -/Engine/...).\r\n\r\nFull depot paths (eg. //depot/...) will not match any files.\r\n\r\nAre you sure you want to continue?", "Invalid view", MessageBoxButtons.OKCancel) != System.Windows.Forms.DialogResult.OK)
				{
					return;
				}
			}

			GlobalFilter = NewGlobalFilter;
			WorkspaceFilter = NewWorkspaceFilter;

			DialogResult = DialogResult.OK;
		}

		private void CancButton_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
		}

		private void ShowCombinedView_Click(object sender, EventArgs e)
		{
			GetSettings(out FilterSettings NewGlobalFilter, out FilterSettings NewWorkspaceFilter);

			string[] Filter = UserSettings.GetCombinedSyncFilter(UniqueIdToCategory, NewGlobalFilter, NewWorkspaceFilter);
			if(Filter.Length == 0)
			{
				Filter = new string[]{ "All files will be synced." };
			}
			MessageBox.Show(String.Join("\r\n", Filter), "Combined View");
		}
	}
}
