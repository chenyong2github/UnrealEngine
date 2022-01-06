// Copyright Epic Games, Inc. All Rights Reserved.

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
	partial class AutomatedSyncWindow : Form
	{
		static class FindDefaultWorkspaceTask
		{
			public static async Task<string?> RunAsync(IPerforceConnection Perforce, string StreamName, CancellationToken CancellationToken)
			{
				InfoRecord Info = await Perforce.GetInfoAsync(InfoOptions.ShortOutput, CancellationToken);

				List<ClientsRecord> Clients = await Perforce.GetClientsAsync(ClientsOptions.None, Perforce.Settings.UserName, CancellationToken);

				List<ClientsRecord> CandidateClients = new List<ClientsRecord>();
				foreach(ClientsRecord Client in Clients)
				{
					if(Client.Host == null || Client.Host.Equals(Info.ClientHost, StringComparison.OrdinalIgnoreCase))
					{
						if(Client.Stream != null && Client.Stream.Equals(StreamName, StringComparison.OrdinalIgnoreCase))
						{
							CandidateClients.Add(Client);
						}
					}
				}

				string? WorkspaceName = null;
				if(CandidateClients.Count >= 1)
				{
					WorkspaceName = CandidateClients.OrderByDescending(x => x.Access).First().Name;
				}
				return WorkspaceName;
			}
		}

		class ValidateWorkspaceTask
		{
			public string WorkspaceName;
			public string StreamName;
			public bool bRequiresStreamSwitch;
			public bool bHasOpenFiles;

			public ValidateWorkspaceTask(string WorkspaceName, string StreamName)
			{
				this.WorkspaceName = WorkspaceName;
				this.StreamName = StreamName;
			}

			public async Task RunAsync(IPerforceConnection Perforce, CancellationToken CancellationToken)
			{
				ClientRecord Spec = await Perforce.GetClientAsync(WorkspaceName, CancellationToken);

				string? CurrentStreamName = Spec.Stream;
				if(CurrentStreamName == null || CurrentStreamName != StreamName)
				{
					bRequiresStreamSwitch = true;

					List<PerforceResponse<OpenedRecord>> Records = await Perforce.TryOpenedAsync(OpenedOptions.None, FileSpecList.Any, CancellationToken).ToListAsync(CancellationToken);
					bHasOpenFiles = Records.Succeeded() && Records.Count > 0;
				}
			}
		}

		public class WorkspaceInfo
		{
			public string ServerAndPort { get; }
			public string UserName { get; }
			public string WorkspaceName { get; }
			public bool bRequiresStreamSwitch { get; }

			public WorkspaceInfo(string ServerAndPort, string UserName, string WorkspaceName, bool bRequiresStreamSwitch)
			{
				this.ServerAndPort = ServerAndPort;
				this.UserName = UserName;
				this.WorkspaceName = WorkspaceName;
				this.bRequiresStreamSwitch = bRequiresStreamSwitch;
			}
		}

		string StreamName;
		IServiceProvider ServiceProvider;

		string? ServerAndPortOverride;
		string? UserNameOverride;
		IPerforceSettings DefaultPerforceSettings;
		WorkspaceInfo? SelectedWorkspaceInfo;

		private AutomatedSyncWindow(string StreamName, string ProjectPath, string? WorkspaceName, IPerforceSettings DefaultPerforceSettings, IServiceProvider ServiceProvider)
		{
			this.StreamName = StreamName;
			this.DefaultPerforceSettings = DefaultPerforceSettings;
			this.ServiceProvider = ServiceProvider;

			InitializeComponent();

			ActiveControl = WorkspaceNameTextBox;

			MinimumSize = Size;
			MaximumSize = new Size(32768, Size.Height);

			ProjectTextBox.Text = StreamName + ProjectPath;

			if(WorkspaceName != null)
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
				WorkspaceNameTextBox.Select(WorkspaceNameTextBox.Text.Length, 0);
			}

			UpdateServerLabel();
			UpdateOkButton();
		}

		private IPerforceSettings Perforce => Utility.OverridePerforceSettings(DefaultPerforceSettings, ServerAndPortOverride, UserNameOverride);

		public static string? FindDefaultWorkspace(IWin32Window Owner, IPerforceSettings DefaultPerforceSettings, string StreamName, IServiceProvider ServiceProvider)
		{
			ILogger Logger = ServiceProvider.GetRequiredService<ILogger<AutomatedSyncWindow>>();
			ModalTask<string?>? WorkspaceTask = PerforceModalTask.Execute(Owner, "Finding workspace", "Finding default workspace, please wait...", DefaultPerforceSettings, (p, c) => FindDefaultWorkspaceTask.RunAsync(p, StreamName, c), Logger);
			return (WorkspaceTask != null && WorkspaceTask.Succeeded) ? WorkspaceTask.Result : null;
		}

		public static bool ShowModal(IWin32Window Owner, IPerforceSettings DefaultPerforceSettings, string StreamName, string ProjectPath, [NotNullWhen(true)] out WorkspaceInfo? WorkspaceInfo, IServiceProvider ServiceProvider)
		{
			string? WorkspaceName = FindDefaultWorkspace(Owner, DefaultPerforceSettings, StreamName, ServiceProvider);

			AutomatedSyncWindow Window = new AutomatedSyncWindow(StreamName, ProjectPath, WorkspaceName, DefaultPerforceSettings, ServiceProvider);
			if(Window.ShowDialog() == DialogResult.OK)
			{
				WorkspaceInfo = Window.SelectedWorkspaceInfo!;
				return true;
			}
			else
			{
				WorkspaceInfo = null;
				return false;
			}
		}

		private void ChangeLink_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			if(ConnectWindow.ShowModal(this, DefaultPerforceSettings, ref ServerAndPortOverride, ref UserNameOverride, ServiceProvider))
			{
				UpdateServerLabel();
			}
		}

		private void UpdateServerLabel()
		{
			ServerLabel.Text = OpenProjectWindow.GetServerLabelText(DefaultPerforceSettings, ServerAndPortOverride, UserNameOverride);
			ChangeLink.Location = new Point(ServerLabel.Right + 5, ChangeLink.Location.Y);
		}

		private void WorkspaceNameNewBtn_Click(object sender, EventArgs e)
		{
			string? WorkspaceName;
			if(NewWorkspaceWindow.ShowModal(this, DefaultPerforceSettings, StreamName, WorkspaceNameTextBox.Text, ServiceProvider, out WorkspaceName))
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
				UpdateOkButton();
			}
		}

		private void WorkspaceNameBrowseBtn_Click(object sender, EventArgs e)
		{
			string? WorkspaceName = WorkspaceNameTextBox.Text;
			if(SelectWorkspaceWindow.ShowModal(this, DefaultPerforceSettings, WorkspaceName, ServiceProvider, out WorkspaceName))
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
				UpdateOkButton();
			}
		}

		private void UpdateOkButton()
		{
			OkBtn.Enabled = (WorkspaceNameTextBox.Text.Length > 0);
		}

		public static bool ValidateWorkspace(IWin32Window Owner, IPerforceSettings Perforce, string WorkspaceName, string StreamName, IServiceProvider ServiceProvider, [NotNullWhen(true)] out WorkspaceInfo? SelectedWorkspaceInfo)
		{
			ValidateWorkspaceTask ValidateWorkspace = new ValidateWorkspaceTask(WorkspaceName, StreamName);

			ModalTask? Task = PerforceModalTask.Execute(Owner, "Checking workspace", "Checking workspace, please wait...", Perforce, ValidateWorkspace.RunAsync, ServiceProvider.GetRequiredService<ILogger<ValidateWorkspaceTask>>());
			if (Task != null && Task.Succeeded)
			{
				if (ValidateWorkspace.bRequiresStreamSwitch)
				{
					string Message;
					if (ValidateWorkspace.bHasOpenFiles)
					{
						Message = String.Format("You have files open for edit in this workspace. If you switch this workspace to {0}, you will not be able to submit them until you switch back.\n\nContinue switching streams?", StreamName);
					}
					else
					{
						Message = String.Format("Switch this workspace to {0}?", StreamName);
					}
					if (MessageBox.Show(Message, "Switch Streams", MessageBoxButtons.YesNo) != DialogResult.Yes)
					{
						SelectedWorkspaceInfo = null;
						return false;
					}
				}

				SelectedWorkspaceInfo = new WorkspaceInfo(Perforce.ServerAndPort, Perforce.UserName, ValidateWorkspace.WorkspaceName, ValidateWorkspace.bRequiresStreamSwitch);
				return true;
			}

			SelectedWorkspaceInfo = null;
			return false;
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			if(ValidateWorkspace(this, Perforce, WorkspaceNameTextBox.Text, StreamName, ServiceProvider, out SelectedWorkspaceInfo))
			{
				DialogResult = DialogResult.OK;
				Close();
			}
		}
	}
}
