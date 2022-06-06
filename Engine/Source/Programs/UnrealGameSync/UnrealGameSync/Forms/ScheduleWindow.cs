// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	partial class ScheduleWindow : Form
	{

		Dictionary<UserSelectedProjectSettings, List<LatestChangeType>> ProjectToLatestChangeTypes;

		public ScheduleWindow(
			bool InEnabled,
			TimeSpan InTime,
			bool AnyOpenProject,
			IEnumerable<UserSelectedProjectSettings> ScheduledProjects,
			IEnumerable<UserSelectedProjectSettings> OpenProjects,
			Dictionary<UserSelectedProjectSettings, List<LatestChangeType>> InProjectToLatestChangeTypes)
		{
			InitializeComponent();

			EnableCheckBox.Checked = InEnabled;

			ProjectToLatestChangeTypes = InProjectToLatestChangeTypes;

			DateTime CurrentTime = DateTime.Now;
			TimePicker.CustomFormat = CultureInfo.CurrentCulture.DateTimeFormat.ShortTimePattern;
			TimePicker.Value = new DateTime(CurrentTime.Year, CurrentTime.Month, CurrentTime.Day, InTime.Hours, InTime.Minutes, InTime.Seconds);

			ProjectListBox.Items.Add("Any open projects", AnyOpenProject);

			Dictionary<string, UserSelectedProjectSettings> LocalFileToProject = new Dictionary<string, UserSelectedProjectSettings>(StringComparer.InvariantCultureIgnoreCase);
			AddProjects(ScheduledProjects, LocalFileToProject);
			AddProjects(OpenProjects, LocalFileToProject);

			foreach(UserSelectedProjectSettings Project in LocalFileToProject.Values.OrderBy(x => x.ToString()))
			{
				bool Enabled = ScheduledProjects.Any(x => x.LocalPath == Project.LocalPath);
				ProjectListBox.Items.Add(Project, Enabled);
			}

			ProjectListBox.ItemCheck += ProjectListBox_ItemCheck;

			SyncTypeDropDown.Closed += SyncTypeDropDown_Closed;

			UpdateEnabledControls();
		}

		private void UpdateSyncTypeDropDownWithProject(UserSelectedProjectSettings? SelectedProject)
		{
			SyncTypeDropDown.Items.Clear();

			if (SelectedProject != null
				&& ProjectToLatestChangeTypes.ContainsKey(SelectedProject))
			{
				foreach (LatestChangeType ChangeType in ProjectToLatestChangeTypes[SelectedProject])
				{
					System.Windows.Forms.ToolStripMenuItem MenuItem = new System.Windows.Forms.ToolStripMenuItem();
					MenuItem.Name = ChangeType.Name;
					MenuItem.Text = ChangeType.Description;
					MenuItem.Size = new System.Drawing.Size(189, 22);
					MenuItem.Click += (sender, e) => SyncTypeDropDown_Click(sender, e, ChangeType.Name);

					SyncTypeDropDown.Items.Add(MenuItem);
				}
			}
		}

		private void ProjectListBox_ItemCheck(object? sender, ItemCheckEventArgs e)
		{
			bool bIsAnyOpenProjectIndex = e.Index == 0;
			if (e.NewValue == CheckState.Checked && !bIsAnyOpenProjectIndex)
			{
				UpdateSyncTypeDropDownWithProject(ProjectListBox.Items[e.Index] as UserSelectedProjectSettings);

				SyncTypeDropDown.Show(
					ProjectListBox,
					ProjectListBox.GetItemRectangle(e.Index).Location,
					ToolStripDropDownDirection.BelowRight);
			}
		}

		private void SyncTypeDropDown_Closed(object? sender, ToolStripDropDownClosedEventArgs e)
		{
			if (e.CloseReason != ToolStripDropDownCloseReason.ItemClicked)
			{
				ProjectListBox.SetItemChecked(ProjectListBox.SelectedIndex, false);
			}
		}

		private void SyncTypeDropDown_Click(object? sender, EventArgs e, string SyncTypeID)
		{
			UserSelectedProjectSettings? ProjectSetting = ProjectListBox.Items[ProjectListBox.SelectedIndex] as UserSelectedProjectSettings;
			if (ProjectSetting != null)
			{
				ProjectSetting.ScheduledSyncTypeID = SyncTypeID;
			}
			SyncTypeDropDown.Close();
		}

		private void AddProjects(IEnumerable<UserSelectedProjectSettings> Projects, Dictionary<string, UserSelectedProjectSettings> LocalFileToProject)
		{
			foreach(UserSelectedProjectSettings Project in Projects)
			{
				if(Project.LocalPath != null)
				{
					LocalFileToProject[Project.LocalPath] = Project;
				}
			}
		}

		public void CopySettings(out bool OutEnabled, out TimeSpan OutTime, out bool OutAnyOpenProject, out List<UserSelectedProjectSettings> OutScheduledProjects)
		{
			OutEnabled = EnableCheckBox.Checked;
			OutTime = TimePicker.Value.TimeOfDay;

			OutAnyOpenProject = false;

			List<UserSelectedProjectSettings> ScheduledProjects = new List<UserSelectedProjectSettings>();
			foreach(int Index in ProjectListBox.CheckedIndices.OfType<int>())
			{
				if(Index == 0)
				{
					OutAnyOpenProject = true;
				}
				else
				{
					ScheduledProjects.Add((UserSelectedProjectSettings)ProjectListBox.Items[Index]);
				}
			}
			OutScheduledProjects = ScheduledProjects;
		}

		private void EnableCheckBox_CheckedChanged(object sender, EventArgs e)
		{
			UpdateEnabledControls();
		}

		private void UpdateEnabledControls()
		{
			TimePicker.Enabled = EnableCheckBox.Checked;
			ProjectListBox.Enabled = EnableCheckBox.Checked;
		}
	}
}
