// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	partial class AutomatedSyncWindow : Form
	{
		class FindDefaultWorkspaceTask : IPerforceModalTask
		{
			string StreamName;
			public string WorkspaceName;

			public FindDefaultWorkspaceTask(string StreamName)
			{
				this.StreamName = StreamName;
			}

			public bool Run(PerforceConnection Perforce, TextWriter Log, out string ErrorMessage)
			{
				PerforceInfoRecord Info;
				if(!Perforce.Info(out Info, Log))
				{
					ErrorMessage = "Unable to query Perforce info.";
					return false;
				}
				
				List<PerforceClientRecord> Clients;
				if(!Perforce.FindClients(Info.UserName, out Clients, Log))
				{
					ErrorMessage = "Unable to enumerate clients from Perforce";
					return false;
				}

				List<PerforceClientRecord> CandidateClients = new List<PerforceClientRecord>();
				foreach(PerforceClientRecord Client in Clients)
				{
					if(Client.Host == null || Client.Host.Equals(Info.HostName, StringComparison.OrdinalIgnoreCase))
					{
						if(Client.Stream != null && Client.Stream.Equals(StreamName, StringComparison.OrdinalIgnoreCase))
						{
							CandidateClients.Add(Client);
						}
					}
				}

				if(CandidateClients.Count >= 1)
				{
					WorkspaceName = CandidateClients.OrderByDescending(x => x.Access).First().Name;
				}

				ErrorMessage = null;
				return true;
			}
		}

		class ValidateWorkspaceTask : IPerforceModalTask
		{
			public string WorkspaceName;
			public string StreamName;
			public string ServerAndPort;
			public string UserName;
			public bool bRequiresStreamSwitch;
			public bool bHasOpenFiles;

			public ValidateWorkspaceTask(string WorkspaceName, string StreamName)
			{
				this.WorkspaceName = WorkspaceName;
				this.StreamName = StreamName;
			}

			public bool Run(PerforceConnection Perforce, TextWriter Log, out string ErrorMessage)
			{
				this.ServerAndPort = Perforce.ServerAndPort;
				this.UserName = Perforce.UserName;

				PerforceSpec Spec;
				if(!Perforce.TryGetClientSpec(WorkspaceName, out Spec, Log))
				{
					ErrorMessage = String.Format("Unable to get info for client '{0}'", WorkspaceName);
					return false;
				}

				string CurrentStreamName = Spec.GetField("Stream");
				if(CurrentStreamName == null || CurrentStreamName != StreamName)
				{
					bRequiresStreamSwitch = true;
					bHasOpenFiles = Perforce.HasOpenFiles(Log);
				}
			
				ErrorMessage = null;
				return true;
			}
		}

		public class WorkspaceInfo
		{
			public string ServerAndPort;
			public string UserName;
			public string WorkspaceName;
			public bool bRequiresStreamSwitch;
		}

		string StreamName;
		TextWriter Log;

		string ServerAndPortOverride;
		string UserNameOverride;
        PerforceConnection DefaultConnection;
		WorkspaceInfo SelectedWorkspaceInfo;

		private AutomatedSyncWindow(string StreamName, string ProjectPath, string WorkspaceName, PerforceConnection DefaultConnection, TextWriter Log)
		{
			this.StreamName = StreamName;
			this.DefaultConnection = DefaultConnection;
			this.Log = Log;

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

		private PerforceConnection Perforce
		{
			get { return Utility.OverridePerforceSettings(DefaultConnection, ServerAndPortOverride, UserNameOverride); }
		}

		public static string FindDefaultWorkspace(IWin32Window Owner, PerforceConnection DefaultConnection, string StreamName, TextWriter Log)
		{
			FindDefaultWorkspaceTask FindWorkspace = new FindDefaultWorkspaceTask(StreamName);

			string ErrorMessage;
			PerforceModalTask.Execute(Owner, DefaultConnection, FindWorkspace, "Finding workspace", "Finding default workspace, please wait...", Log, out ErrorMessage);

			return FindWorkspace.WorkspaceName;
		}

		public static bool ShowModal(IWin32Window Owner, PerforceConnection DefaultConnection, string StreamName, string ProjectPath, out WorkspaceInfo WorkspaceInfo, TextWriter Log)
		{
			string WorkspaceName = FindDefaultWorkspace(Owner, DefaultConnection, StreamName, Log);

			AutomatedSyncWindow Window = new AutomatedSyncWindow(StreamName, ProjectPath, WorkspaceName, DefaultConnection, Log);
			if(Window.ShowDialog() == DialogResult.OK)
			{
				WorkspaceInfo = Window.SelectedWorkspaceInfo;
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
			if(ConnectWindow.ShowModal(this, DefaultConnection, ref ServerAndPortOverride, ref UserNameOverride, Log))
			{
				UpdateServerLabel();
			}
		}

		private void UpdateServerLabel()
		{
			ServerLabel.Text = OpenProjectWindow.GetServerLabelText(DefaultConnection, ServerAndPortOverride, UserNameOverride);
			ChangeLink.Location = new Point(ServerLabel.Right + 5, ChangeLink.Location.Y);
		}

		private void WorkspaceNameNewBtn_Click(object sender, EventArgs e)
		{
			string WorkspaceName;
			if(NewWorkspaceWindow.ShowModal(this, Perforce, StreamName, WorkspaceNameTextBox.Text, Log, out WorkspaceName))
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
				UpdateOkButton();
			}
		}

		private void WorkspaceNameBrowseBtn_Click(object sender, EventArgs e)
		{
			string WorkspaceName = WorkspaceNameTextBox.Text;
			if(SelectWorkspaceWindow.ShowModal(this, Perforce, WorkspaceName, Log, out WorkspaceName))
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
				UpdateOkButton();
			}
		}

		private void UpdateOkButton()
		{
			OkBtn.Enabled = (WorkspaceNameTextBox.Text.Length > 0);
		}

		public static bool ValidateWorkspace(IWin32Window Owner, PerforceConnection Perforce, string WorkspaceName, string StreamName, TextWriter Log, out WorkspaceInfo SelectedWorkspaceInfo)
		{
			ValidateWorkspaceTask ValidateWorkspace = new ValidateWorkspaceTask(WorkspaceName, StreamName);

			string ErrorMessage;
			ModalTaskResult Result = PerforceModalTask.Execute(Owner, Perforce, ValidateWorkspace, "Checking workspace", "Checking workspace, please wait...", Log, out ErrorMessage);
			if (Result == ModalTaskResult.Failed)
			{
				MessageBox.Show(ErrorMessage);
			}
			else if (Result == ModalTaskResult.Succeeded)
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

				SelectedWorkspaceInfo = new WorkspaceInfo() { ServerAndPort = ValidateWorkspace.ServerAndPort, UserName = ValidateWorkspace.UserName, WorkspaceName = ValidateWorkspace.WorkspaceName, bRequiresStreamSwitch = ValidateWorkspace.bRequiresStreamSwitch };
				return true;
			}

			SelectedWorkspaceInfo = null;
			return false;
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			if(ValidateWorkspace(this, Perforce, WorkspaceNameTextBox.Text, StreamName, Log, out SelectedWorkspaceInfo))
			{
				DialogResult = DialogResult.OK;
				Close();
			}
		}
	}
}
