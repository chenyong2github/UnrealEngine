// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
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
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

#nullable enable

namespace UnrealGameSync
{
	partial class OpenProjectWindow : Form
	{
		string? ServerAndPortOverride;
		string? UserNameOverride;
		OpenProjectInfo? OpenProjectInfo;
		IPerforceSettings DefaultSettings;
		IServiceProvider ServiceProvider;
		UserSettings Settings;
		ILogger Logger;

		private OpenProjectWindow(UserSelectedProjectSettings? Project, UserSettings Settings, IPerforceSettings DefaultSettings, IServiceProvider ServiceProvider, ILogger Logger)
		{
			InitializeComponent();

			this.Settings = Settings;
			this.OpenProjectInfo = null;
			this.DefaultSettings = DefaultSettings;
			this.ServiceProvider = ServiceProvider;
			this.Logger = Logger;

			if (Project == null)
			{
				LocalFileRadioBtn.Checked = true;
			}
			else
			{
				if(!String.IsNullOrWhiteSpace(Project.ServerAndPort))
				{
					ServerAndPortOverride = Project.ServerAndPort;
				}
				if(!String.IsNullOrWhiteSpace(Project.UserName))
				{
					UserNameOverride = Project.UserName;
				}

				if(Project.ClientPath != null && Project.ClientPath.StartsWith("//"))
				{
					int SlashIdx = Project.ClientPath.IndexOf('/', 2);
					if(SlashIdx != -1)
					{
						WorkspaceNameTextBox.Text = Project.ClientPath.Substring(2, SlashIdx - 2);
						WorkspacePathTextBox.Text = Project.ClientPath.Substring(SlashIdx);
					}
				}

				if(Project.LocalPath != null)
				{
					LocalFileTextBox.Text = Project.LocalPath;
				}

				if(Project.Type == UserSelectedProjectType.Client)
				{
					WorkspaceRadioBtn.Checked = true;
				}
				else
				{
					LocalFileRadioBtn.Checked = true;
				}
			}

			UpdateEnabledControls();
			UpdateServerLabel();
			UpdateWorkspacePathBrowseButton();
			UpdateOkButton();
		}

		private IPerforceSettings Perforce
		{
			get => Utility.OverridePerforceSettings(DefaultSettings, ServerAndPortOverride, UserNameOverride);
		}

		public static OpenProjectInfo? ShowModal(IWin32Window Owner, UserSelectedProjectSettings? Project, UserSettings Settings, DirectoryReference DataFolder, DirectoryReference CacheFolder, IPerforceSettings DefaultPerforceSettings, IServiceProvider ServiceProvider, ILogger Logger)
		{
			OpenProjectWindow Window = new OpenProjectWindow(Project, Settings, DefaultPerforceSettings, ServiceProvider, Logger);
			if(Window.ShowDialog(Owner) == DialogResult.OK)
			{
				return Window.OpenProjectInfo;
			}
			else
			{
				return null;
			}
		}

		private void UpdateEnabledControls()
		{
			Color WorkspaceTextColor = WorkspaceRadioBtn.Checked? SystemColors.ControlText : SystemColors.GrayText;
			WorkspaceNameLabel.ForeColor = WorkspaceTextColor;
			WorkspaceNameTextBox.ForeColor = WorkspaceTextColor;
			WorkspaceNameNewBtn.ForeColor = WorkspaceTextColor;
			WorkspaceNameBrowseBtn.ForeColor = WorkspaceTextColor;
			WorkspacePathLabel.ForeColor = WorkspaceTextColor;
			WorkspacePathTextBox.ForeColor = WorkspaceTextColor;
			WorkspacePathBrowseBtn.ForeColor = WorkspaceTextColor;

			Color LocalFileTextColor = LocalFileRadioBtn.Checked? SystemColors.ControlText : SystemColors.GrayText;
			LocalFileLabel.ForeColor = LocalFileTextColor;
			LocalFileTextBox.ForeColor = LocalFileTextColor;
			LocalFileBrowseBtn.ForeColor = LocalFileTextColor;

			UpdateWorkspacePathBrowseButton();
		}

		public static string GetServerLabelText(IPerforceSettings DefaultSettings, string? ServerAndPort, string? UserName)
		{
			if(ServerAndPort == null && UserName == null)
			{
				return String.Format("Using default connection settings (user '{0}' on server '{1}').", DefaultSettings.UserName, DefaultSettings.ServerAndPort);
			}
			else
			{
				StringBuilder Text = new StringBuilder("Connecting as ");
				if(UserName == null)
				{
					Text.Append("default user");
				}
				else
				{
					Text.AppendFormat("user '{0}'", UserName);
				}
				Text.Append(" on ");
				if(ServerAndPort == null)
				{
					Text.Append("default server.");
				}
				else
				{
					Text.AppendFormat("server '{0}'.", ServerAndPort);
				}
				return Text.ToString();
			}
		}

		private void UpdateServerLabel()
		{
			ServerLabel.Text = GetServerLabelText(DefaultSettings, ServerAndPortOverride, UserNameOverride);
		}

		private void UpdateWorkspacePathBrowseButton()
		{
			string? WorkspaceName;
			WorkspacePathBrowseBtn.Enabled = TryGetWorkspaceName(out WorkspaceName);
		}

		private void UpdateOkButton()
		{
			string? ProjectPath;
			OkBtn.Enabled = WorkspaceRadioBtn.Checked? TryGetClientPath(out ProjectPath) : TryGetLocalPath(out ProjectPath);
		}

		private void WorkspaceNewBtn_Click(object sender, EventArgs e)
		{
			WorkspaceRadioBtn.Checked = true;
			
			string? WorkspaceName;
			if(NewWorkspaceWindow.ShowModal(this, Perforce, null, WorkspaceNameTextBox.Text, ServiceProvider, out WorkspaceName))
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
				UpdateOkButton();
			}
		}

		private void WorkspaceBrowseBtn_Click(object sender, EventArgs e)
		{
			WorkspaceRadioBtn.Checked = true;

			string? WorkspaceName = WorkspaceNameTextBox.Text;
			if(SelectWorkspaceWindow.ShowModal(this, Perforce, WorkspaceName, ServiceProvider, out WorkspaceName))
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
			}
		}

		private void WorkspacePathBrowseBtn_Click(object sender, EventArgs e)
		{
			WorkspaceRadioBtn.Checked = true;

			string? WorkspaceName;
			if(TryGetWorkspaceName(out WorkspaceName))
			{
				string? WorkspacePath = WorkspacePathTextBox.Text.Trim();
				if(SelectProjectFromWorkspaceWindow.ShowModal(this, Perforce, WorkspaceName, WorkspacePath, ServiceProvider, out WorkspacePath))
				{
					WorkspacePathTextBox.Text = WorkspacePath;
					UpdateOkButton();
				}
			}
		}

		private bool TryGetWorkspaceName([NotNullWhen(true)] out string? WorkspaceName)
		{
			string Text = WorkspaceNameTextBox.Text.Trim();
			if(Text.Length == 0)
			{
				WorkspaceName = null;
				return false;
			}

			WorkspaceName = Text;
			return true;
		}

		private bool TryGetClientPath([NotNullWhen(true)] out string? ClientPath)
		{
			string? WorkspaceName;
			if(!TryGetWorkspaceName(out WorkspaceName))
			{
				ClientPath = null;
				return false;
			}

			string WorkspacePath = WorkspacePathTextBox.Text.Trim();
			if(WorkspacePath.Length == 0 || WorkspacePath[0] != '/')
			{
				ClientPath = null;
				return false;
			}

			ClientPath = String.Format("//{0}{1}", WorkspaceName, WorkspacePath);
			return true;
		}

		private bool TryGetLocalPath(out string? LocalPath)
		{
			string LocalFile = LocalFileTextBox.Text.Trim();
			if(LocalFile.Length == 0)
			{
				LocalPath = null;
				return false;
			}

			LocalPath = Path.GetFullPath(LocalFile);
			return true;
		}

		private bool TryGetSelectedProject([NotNullWhen(true)] out UserSelectedProjectSettings? Project)
		{
			if(WorkspaceRadioBtn.Checked)
			{
				string? ClientPath;
				if(TryGetClientPath(out ClientPath))
				{
					Project = new UserSelectedProjectSettings(ServerAndPortOverride, UserNameOverride, UserSelectedProjectType.Client, ClientPath, null);
					return true;
				}
			}
			else
			{
				string? LocalPath;
				if(TryGetLocalPath(out LocalPath))
				{
					Project = new UserSelectedProjectSettings(ServerAndPortOverride, UserNameOverride, UserSelectedProjectType.Local, null, LocalPath);
					return true;
				}
			}

			Project = null;
			return false;
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			UserSelectedProjectSettings? SelectedProject;
			if(TryGetSelectedProject(out SelectedProject))
			{
				ILogger<OpenProjectInfo> Logger = ServiceProvider.GetRequiredService<ILogger<OpenProjectInfo>>();

				PerforceSettings NewPerforceSettings = Utility.OverridePerforceSettings(Perforce, SelectedProject.ServerAndPort, SelectedProject.UserName);

				ModalTask<OpenProjectInfo>? NewOpenProjectInfo = PerforceModalTask.Execute(this, "Opening project", "Opening project, please wait...", NewPerforceSettings, (x, y) => DetectSettingsAsync(x, SelectedProject, Settings, Logger, y), Logger);
				if (NewOpenProjectInfo != null && NewOpenProjectInfo.Succeeded)
				{
					OpenProjectInfo = NewOpenProjectInfo.Result;
					DialogResult = DialogResult.OK;
					Close();
				}
			}
		}

		public static async Task<OpenProjectInfo> DetectSettingsAsync(IPerforceConnection Perforce, UserSelectedProjectSettings SelectedProject, UserSettings UserSettings, ILogger<OpenProjectInfo> Logger, CancellationToken CancellationToken)
		{
			OpenProjectInfo Settings = await OpenProjectInfo.CreateAsync(Perforce, SelectedProject, UserSettings, Logger, CancellationToken);
			if (DeploymentSettings.OnDetectProjectSettings != null)
			{
				string? Message;
				if (!DeploymentSettings.OnDetectProjectSettings(Settings, Logger, out Message))
				{
					throw new UserErrorException(Message);
				}
			}
			return Settings;
		}

		private void ChangeLink_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			if(ConnectWindow.ShowModal(this, DefaultSettings, ref ServerAndPortOverride, ref UserNameOverride, ServiceProvider))
			{
				UpdateServerLabel();
			}
		}

		private void LocalFileBrowseBtn_Click(object sender, EventArgs e)
		{
			LocalFileRadioBtn.Checked = true;

			OpenFileDialog Dialog = new OpenFileDialog();
			Dialog.Filter = "Project files (*.uproject)|*.uproject|Project directory lists (*.uprojectdirs)|*.uprojectdirs|All supported files (*.uproject;*.uprojectdirs)|*.uproject;*.uprojectdirs|All files (*.*)|*.*" ;
			Dialog.FilterIndex = Settings.FilterIndex;
			
			if(!String.IsNullOrEmpty(LocalFileTextBox.Text))
			{
				try
				{
					Dialog.InitialDirectory = Path.GetDirectoryName(LocalFileTextBox.Text);
				}
				catch
				{
				}
			}

			if (Dialog.ShowDialog(this) == DialogResult.OK)
			{
				string FullName = Path.GetFullPath(Dialog.FileName);

				Settings.FilterIndex = Dialog.FilterIndex;
				Settings.Save(Logger);

				LocalFileTextBox.Text = FullName;
				UpdateOkButton();
			}
		}

		private void WorkspaceNameTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
			UpdateWorkspacePathBrowseButton();
		}

		private void WorkspacePathTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
		}

		private void LocalFileTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
		}

		private void WorkspaceRadioBtn_CheckedChanged(object sender, EventArgs e)
		{
			UpdateEnabledControls();
		}

		private void LocalFileRadioBtn_CheckedChanged(object sender, EventArgs e)
		{
			UpdateEnabledControls();
		}

		private void LocalFileTextBox_Enter(object sender, EventArgs e)
		{
			LocalFileRadioBtn.Checked = true;
		}

		private void WorkspaceNameTextBox_Enter(object sender, EventArgs e)
		{
			WorkspaceRadioBtn.Checked = true;
		}

		private void WorkspacePathTextBox_Enter(object sender, EventArgs e)
		{
			WorkspaceRadioBtn.Checked = true;
		}
	}
}
