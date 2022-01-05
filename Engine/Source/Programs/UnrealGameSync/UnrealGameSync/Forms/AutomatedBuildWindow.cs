// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using UnrealGameSync.Properties;

namespace UnrealGameSync.Forms
{
	partial class AutomatedBuildWindow : Form
	{
		public class BuildInfo
		{
			public AutomatedSyncWindow.WorkspaceInfo SelectedWorkspaceInfo;
			public string ProjectPath;
			public bool bSync;
			public string ExecCommand;

			public BuildInfo(AutomatedSyncWindow.WorkspaceInfo SelectedWorkspaceInfo, string ProjectPath, bool bSync, string ExecCommand)
			{
				this.SelectedWorkspaceInfo = SelectedWorkspaceInfo;
				this.ProjectPath = ProjectPath;
				this.bSync = bSync;
				this.ExecCommand = ExecCommand;
			}
		}

		string StreamName;
		IServiceProvider ServiceProvider;

		string? ServerAndPortOverride;
		string? UserNameOverride;
		IPerforceSettings DefaultPerforceSettings;

		BuildInfo? Result;

		private AutomatedBuildWindow(string StreamName, int Changelist, string Command, IPerforceSettings DefaultPerforceSettings, string? DefaultWorkspaceName, string? DefaultProjectPath, IServiceProvider ServiceProvider)
		{
			this.StreamName = StreamName;
			this.DefaultPerforceSettings = DefaultPerforceSettings;
			this.ServiceProvider = ServiceProvider;

			InitializeComponent();

			ActiveControl = WorkspaceNameTextBox;

			MinimumSize = Size;
			MaximumSize = new Size(32768, Size.Height);

			SyncToChangeCheckBox.Text = String.Format("Sync to changelist {0}", Changelist);
			ExecCommandTextBox.Text = Command;

			if (DefaultWorkspaceName != null)
			{
				WorkspaceNameTextBox.Text = DefaultWorkspaceName;
				WorkspaceNameTextBox.Select(WorkspaceNameTextBox.Text.Length, 0);
			}

			if (DefaultProjectPath != null)
			{
				WorkspacePathTextBox.Text = DefaultProjectPath;
				WorkspacePathTextBox.Select(WorkspacePathTextBox.Text.Length, 0);
			}

			UpdateServerLabel();
			UpdateOkButton();
			UpdateWorkspacePathBrowseButton();
		}

		private IPerforceSettings Perforce
		{
			get => Utility.OverridePerforceSettings(DefaultPerforceSettings, ServerAndPortOverride, UserNameOverride);
		}

		public static bool ShowModal(IWin32Window Owner, IPerforceSettings DefaultPerforceSettings, string StreamName, string ProjectPath, int Changelist, string Command, UserSettings Settings, IServiceProvider LoggerFactory, [NotNullWhen(true)] out BuildInfo? BuildInfo)
		{
			string? DefaultWorkspaceName = AutomatedSyncWindow.FindDefaultWorkspace(Owner, DefaultPerforceSettings, StreamName, LoggerFactory);

			string? DefaultProjectPath = null;
			if(!String.IsNullOrEmpty(ProjectPath))
			{
				DefaultProjectPath = ProjectPath;
			}
			else if(DefaultWorkspaceName != null)
			{
				string ClientPrefix = String.Format("//{0}/", DefaultWorkspaceName);
				foreach (UserSelectedProjectSettings ProjectSettings in Settings.RecentProjects)
				{
					if (ProjectSettings.ClientPath != null && ProjectSettings.ClientPath.StartsWith(ClientPrefix, StringComparison.OrdinalIgnoreCase))
					{
						DefaultProjectPath = ProjectSettings.ClientPath.Substring(ClientPrefix.Length - 1);
						break;
					}
				}
			}

			AutomatedBuildWindow Window = new AutomatedBuildWindow(StreamName, Changelist, Command, DefaultPerforceSettings, DefaultWorkspaceName, DefaultProjectPath, LoggerFactory);
			if (Window.ShowDialog() == DialogResult.OK)
			{
				BuildInfo = Window.Result!;
				return true;
			}
			else
			{
				BuildInfo = null;
				return false;
			}
		}

		private void ChangeLink_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			if (ConnectWindow.ShowModal(this, DefaultPerforceSettings, ref ServerAndPortOverride, ref UserNameOverride, ServiceProvider))
			{
				UpdateServerLabel();
			}
		}

		private void UpdateServerLabel()
		{
			ServerLabel.Text = OpenProjectWindow.GetServerLabelText(DefaultPerforceSettings, ServerAndPortOverride, UserNameOverride);
		}

		private void WorkspaceNameNewBtn_Click(object sender, EventArgs e)
		{
			string? WorkspaceName;
			if (NewWorkspaceWindow.ShowModal(this, Perforce, StreamName, WorkspaceNameTextBox.Text, ServiceProvider, out WorkspaceName))
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
				UpdateOkButton();
			}
		}

		private void WorkspaceNameBrowseBtn_Click(object sender, EventArgs e)
		{
			string? WorkspaceName = WorkspaceNameTextBox.Text;
			if (SelectWorkspaceWindow.ShowModal(this, Perforce, WorkspaceName, ServiceProvider, out WorkspaceName))
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
				UpdateOkButton();
			}
		}

		private void UpdateOkButton()
		{
			OkBtn.Enabled = (WorkspaceNameTextBox.Text.Length > 0);
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			AutomatedSyncWindow.WorkspaceInfo? SelectedWorkspaceInfo;
			if (AutomatedSyncWindow.ValidateWorkspace(this, Perforce, WorkspaceNameTextBox.Text, StreamName, ServiceProvider, out SelectedWorkspaceInfo))
			{
				Result = new BuildInfo(SelectedWorkspaceInfo, WorkspacePathTextBox.Text, SyncToChangeCheckBox.Checked, ExecCommandTextBox.Text);
				DialogResult = DialogResult.OK;
				Close();
			}
		}

		private void ExecCommandCheckBox_CheckedChanged(object sender, EventArgs e)
		{
			ExecCommandTextBox.Enabled = ExecCommandCheckBox.Checked;
		}

		private void UpdateWorkspacePathBrowseButton()
		{
			string? WorkspaceName;
			WorkspacePathBrowseBtn.Enabled = TryGetWorkspaceName(out WorkspaceName);
		}

		private void WorkspacePathBrowseBtn_Click(object sender, EventArgs e)
		{
			string? WorkspaceName;
			if (TryGetWorkspaceName(out WorkspaceName))
			{
				string? WorkspacePath = WorkspacePathTextBox.Text.Trim();
				if (SelectProjectFromWorkspaceWindow.ShowModal(this, Perforce, WorkspaceName, WorkspacePath, ServiceProvider, out WorkspacePath))
				{
					WorkspacePathTextBox.Text = WorkspacePath;
					UpdateOkButton();
				}
			}
		}

		private bool TryGetWorkspaceName([NotNullWhen(true)] out string? WorkspaceName)
		{
			string Text = WorkspaceNameTextBox.Text.Trim();
			if (Text.Length == 0)
			{
				WorkspaceName = null;
				return false;
			}

			WorkspaceName = Text;
			return true;
		}

		private void WorkspaceNameTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateWorkspacePathBrowseButton();
		}
	}
}
