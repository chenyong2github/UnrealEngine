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

namespace UnrealGameSync
{
	partial class NewWorkspaceWindow : Form
	{
		class NewWorkspaceSettings
		{
			public string Name { get; }
			public string Stream { get; }
			public DirectoryReference RootDir { get; }

			public NewWorkspaceSettings(string Name, string Stream, DirectoryReference RootDir)
			{
				this.Name = Name;
				this.Stream = Stream;
				this.RootDir = RootDir;
			}
		}

		class FindWorkspaceSettingsTask
		{
			public InfoRecord Info;
			public List<ClientsRecord> Clients;
			public string? CurrentStream;

			public FindWorkspaceSettingsTask(InfoRecord Info, List<ClientsRecord> Clients, string? CurrentStream)
			{
				this.Info = Info;
				this.Clients = Clients;
				this.CurrentStream = CurrentStream;
			}

			public static async Task<FindWorkspaceSettingsTask> RunAsync(IPerforceConnection Perforce, string CurrentWorkspaceName, CancellationToken CancellationToken)
			{
				InfoRecord Info = await Perforce.GetInfoAsync(InfoOptions.ShortOutput, CancellationToken);
				List<ClientsRecord> Clients = await Perforce.GetClientsAsync(ClientsOptions.None, Perforce.Settings.UserName, CancellationToken);

				string? CurrentStream = null;
				if(!String.IsNullOrEmpty(CurrentWorkspaceName))
				{
					CurrentStream = await Perforce.GetCurrentStreamAsync(CancellationToken);
				}

				return new FindWorkspaceSettingsTask(Info, Clients, CurrentStream);
			}
		}

		static class NewWorkspaceTask
		{
			public static async Task RunAsync(IPerforceConnection Perforce, NewWorkspaceSettings Settings, string Owner, string HostName, CancellationToken CancellationToken)
			{
				PerforceResponseList<ClientsRecord> Response = await Perforce.TryGetClientsAsync(ClientsOptions.None, Settings.Name, -1, null, null, CancellationToken);
				if (!Response.Succeeded)
				{
					throw new UserErrorException($"Unable to determine if client already exists.\n\n{Response[0]}");
				}
				if (Response.Data.Count > 0)
				{
					throw new UserErrorException($"Client '{Settings.Name}' already exists.");
				}

				ClientRecord Client = new ClientRecord(Settings.Name, Owner, Settings.RootDir.FullName);
				Client.Host = HostName;
				Client.Stream = Settings.Stream;
				Client.Options = ClientOptions.Rmdir;
				await Perforce.CreateClientAsync(Client, CancellationToken);
			}
		}

		IPerforceSettings PerforceSettings;
		InfoRecord Info;
		List<ClientsRecord> Clients;
		IServiceProvider ServiceProvider;
		NewWorkspaceSettings? Settings;
		DirectoryReference? DefaultRootPath;

		private NewWorkspaceWindow(IPerforceSettings PerforceSettings, string? ForceStream, string? DefaultStream, InfoRecord Info, List<ClientsRecord> Clients, IServiceProvider ServiceProvider)
		{
			InitializeComponent();

			this.PerforceSettings = PerforceSettings;
			this.Info = Info;
			this.Clients = Clients;
			this.ServiceProvider = ServiceProvider;

			Dictionary<DirectoryReference, int> RootPathToCount = new Dictionary<DirectoryReference, int>();
			foreach(ClientsRecord Client in Clients)
			{
				if(Client.Host == null || String.Compare(Client.Host, Info.ClientHost, StringComparison.OrdinalIgnoreCase) == 0)
				{
					if(!String.IsNullOrEmpty(Client.Root) && Client.Root != ".")
					{
						DirectoryReference? ParentDir;
						try
						{
							ParentDir = new DirectoryReference(Client.Root);
						}
						catch
						{
							ParentDir = null;
						}

						if(ParentDir != null)
						{
							int Count;
							RootPathToCount.TryGetValue(ParentDir, out Count);
							RootPathToCount[ParentDir] = Count + 1;
						}
					}
				}
			}

			int RootPathMaxCount = 0;
			foreach(KeyValuePair<DirectoryReference, int> RootPathPair in RootPathToCount)
			{
				if(RootPathPair.Value > RootPathMaxCount)
				{
					DefaultRootPath = RootPathPair.Key;
					RootPathMaxCount = RootPathPair.Value;
				}
			}

			if(ForceStream != null)
			{
				StreamTextBox.Text = ForceStream;
				StreamTextBox.Enabled = false;
			}
			else
			{
				StreamTextBox.Text = DefaultStream ?? "";
				StreamTextBox.Enabled = true;
			}
			StreamTextBox.SelectionStart = StreamTextBox.Text.Length;
			StreamTextBox.SelectionLength = 0;
			StreamTextBox.Focus();

			StreamBrowseBtn.Enabled = (ForceStream == null);

			UpdateOkButton();
			UpdateNameCueBanner();
			UpdateRootDirCueBanner();
		}

		public static bool ShowModal(IWin32Window Owner, IPerforceSettings PerforceSettings, string? ForceStreamName, string CurrentWorkspaceName, IServiceProvider ServiceProvider, [NotNullWhen(true)] out string? WorkspaceName)
		{
			ModalTask<FindWorkspaceSettingsTask>? Task = PerforceModalTask.Execute(Owner, "Checking settings", "Checking settings, please wait...", PerforceSettings, (p, c) => FindWorkspaceSettingsTask.RunAsync(p, CurrentWorkspaceName, c), ServiceProvider.GetRequiredService<ILogger<FindWorkspaceSettingsTask>>());
			if (Task == null || !Task.Succeeded)
			{
				WorkspaceName = null;
				return false;
			}

			NewWorkspaceWindow Window = new NewWorkspaceWindow(PerforceSettings, ForceStreamName, Task.Result.CurrentStream, Task.Result.Info, Task.Result.Clients, ServiceProvider);
			if(Window.ShowDialog(Owner) == DialogResult.OK)
			{
				WorkspaceName = Window.Settings!.Name;
				return true;
			}
			else
			{
				WorkspaceName = null;
				return false;
			}
		}

		private void RootDirBrowseBtn_Click(object sender, EventArgs e)
		{
			FolderBrowserDialog Dialog = new FolderBrowserDialog();
			Dialog.ShowNewFolderButton = true;
			Dialog.SelectedPath = RootDirTextBox.Text;
			if (Dialog.ShowDialog() == DialogResult.OK)
			{
				RootDirTextBox.Text = Dialog.SelectedPath;
				UpdateOkButton();
			}
		}

		private string GetDefaultWorkspaceName()
		{
			string BaseName = Sanitize(String.Format("{0}_{1}_{2}", Info.UserName, Info.ClientHost, StreamTextBox.Text.Replace('/', '_').Trim('_'))).Trim('_');

			string Name = BaseName;
			for(int Idx = 2; Clients.Any(x => x.Name != null && String.Compare(x.Name, Name, StringComparison.InvariantCultureIgnoreCase) == 0); Idx++)
			{
				Name = String.Format("{0}_{1}", BaseName, Idx);
			}
			return Name;
		}

		private string GetDefaultWorkspaceRootDir()
		{
			string RootDir = "";
			if(DefaultRootPath != null)
			{
				string Suffix = String.Join("_", StreamTextBox.Text.Split(new char[]{ '/' }, StringSplitOptions.RemoveEmptyEntries).Select(x => Sanitize(x)).Where(x => x.Length > 0));
				if(Suffix.Length > 0)
				{
					RootDir = DirectoryReference.Combine(DefaultRootPath, Suffix).FullName;
				}
			}
			return RootDir;
		}

		private string Sanitize(string Text)
		{
			StringBuilder Result = new StringBuilder();
			for(int Idx = 0; Idx < Text.Length; Idx++)
			{
				if(Char.IsLetterOrDigit(Text[Idx]) || Text[Idx] == '_' || Text[Idx] == '.' || Text[Idx] == '-')
				{
					Result.Append(Text[Idx]);
				}
			}
			return Result.ToString();
		}

		private void UpdateNameCueBanner()
		{
			NameTextBox.CueBanner = GetDefaultWorkspaceName();
		}

		private void UpdateRootDirCueBanner()
		{
			RootDirTextBox.CueBanner = GetDefaultWorkspaceRootDir();
		}

		private void UpdateOkButton()
		{
			NewWorkspaceSettings? Settings;
			OkBtn.Enabled = TryGetWorkspaceSettings(out Settings);
		}

		private bool TryGetWorkspaceSettings([NotNullWhen(true)] out NewWorkspaceSettings? Settings)
		{
			string NewWorkspaceName = NameTextBox.Text.Trim();
			if(NewWorkspaceName.Length == 0)
			{
				NewWorkspaceName = GetDefaultWorkspaceName();
				if(NewWorkspaceName.Length == 0)
				{
					Settings = null;
					return false;
				}
			}

			string NewStream = StreamTextBox.Text.Trim();
			if(!NewStream.StartsWith("//") || NewStream.IndexOf('/', 2) == -1)
			{
				Settings = null;
				return false;
			}

			string NewRootDir = RootDirTextBox.Text.Trim();
			if(NewRootDir.Length == 0)
			{
				NewRootDir = GetDefaultWorkspaceRootDir();
				if(NewRootDir.Length == 0)
				{
					Settings = null;
					return false;
				}
			}

			DirectoryReference NewRootDirRef;
			try
			{
				NewRootDirRef = new DirectoryReference(NewRootDir);
			}
			catch
			{
				Settings = null;
				return false;
			}

			Settings = new NewWorkspaceSettings(NewWorkspaceName, NewStream, NewRootDirRef);
			return true;
		}

		private void NameTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
		}

		private void StreamTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
			UpdateNameCueBanner();
			UpdateRootDirCueBanner();
		}

		private void RootDirTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			if(TryGetWorkspaceSettings(out Settings))
			{
				DirectoryInfo RootDir = Settings.RootDir.ToDirectoryInfo();
				if(RootDir.Exists && RootDir.EnumerateFileSystemInfos().Any(x => x.Name != "." && x.Name != ".."))
				{
					if(MessageBox.Show(this, String.Format("The directory '{0}' is not empty. Are you sure you want to create a workspace there?", RootDir.FullName), "Directory not empty", MessageBoxButtons.YesNo) != DialogResult.Yes)
					{
						return;
					}
				}

				ILogger Logger = ServiceProvider.GetRequiredService<ILogger<NewWorkspaceWindow>>();

				ModalTask? Result = PerforceModalTask.Execute(Owner, "Creating workspace", "Creating workspace, please wait...", PerforceSettings, (p, c) => NewWorkspaceTask.RunAsync(p, Settings, Info.UserName ?? "", Info.ClientHost ?? "", c), Logger);
				if (Result == null || !Result.Succeeded)
				{
					return;
				}

				DialogResult = DialogResult.OK;
				Close();
			}
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
			Close();
		}

		private void StreamBrowseBtn_Click(object sender, EventArgs e)
		{
			string? StreamName = StreamTextBox.Text.Trim();
			if(SelectStreamWindow.ShowModal(this, PerforceSettings, StreamName, ServiceProvider, out StreamName))
			{
				StreamTextBox.Text = StreamName;
			}
		}

		private void RootDirTextBox_Enter(object sender, EventArgs e)
		{
			if(RootDirTextBox.Text.Length == 0)
			{
				RootDirTextBox.Text = RootDirTextBox.CueBanner;
			}
		}

		private void RootDirTextBox_Leave(object sender, EventArgs e)
		{
			if(RootDirTextBox.Text == RootDirTextBox.CueBanner)
			{
				RootDirTextBox.Text = "";
			}
		}

		private void NameTextBox_Enter(object sender, EventArgs e)
		{
			if(NameTextBox.Text.Length == 0)
			{
				NameTextBox.Text = NameTextBox.CueBanner;
			}
		}

		private void NameTextBox_Leave(object sender, EventArgs e)
		{
			if(NameTextBox.Text == NameTextBox.CueBanner)
			{
				NameTextBox.Text = "";
			}
		}
	}
}
