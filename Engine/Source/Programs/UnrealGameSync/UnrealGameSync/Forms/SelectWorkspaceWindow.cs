// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

#nullable enable

namespace UnrealGameSync
{
	partial class SelectWorkspaceWindow : Form
	{
		class EnumerateWorkspaces
		{
			public InfoRecord Info { get; }
			public List<ClientsRecord> Clients { get; }

			public EnumerateWorkspaces(InfoRecord Info, List<ClientsRecord> Clients)
			{
				this.Info = Info;
				this.Clients = Clients;
			}

			public static async Task<EnumerateWorkspaces> RunAsync(IPerforceConnection Perforce, CancellationToken CancellationToken)
			{
				InfoRecord Info = await Perforce.GetInfoAsync(InfoOptions.ShortOutput, CancellationToken);
				List<ClientsRecord> Clients = await Perforce.GetClientsAsync(ClientsOptions.None, Perforce.Settings.UserName, CancellationToken);
				return new EnumerateWorkspaces(Info, Clients);
			}
		}

		InfoRecord Info;
		List<ClientsRecord> Clients;
		string? WorkspaceName;

		private SelectWorkspaceWindow(InfoRecord Info, List<ClientsRecord> Clients, string? WorkspaceName)
		{
			InitializeComponent();

			this.Info = Info;
			this.Clients = Clients;
			this.WorkspaceName = WorkspaceName;

			UpdateListView();
			UpdateOkButton();
		}

		private void UpdateListView()
		{
			if(WorkspaceListView.SelectedItems.Count > 0)
			{
				WorkspaceName = WorkspaceListView.SelectedItems[0].Text;
			}
			else
			{
				WorkspaceName = null;
			}

			WorkspaceListView.Items.Clear();

			foreach(ClientsRecord Client in Clients.OrderBy(x => x.Name))
			{
				if(!OnlyForThisComputer.Checked || String.Compare(Client.Host, Info.ClientHost, StringComparison.InvariantCultureIgnoreCase) == 0)
				{
					ListViewItem Item = new ListViewItem(Client.Name);
					Item.SubItems.Add(new ListViewItem.ListViewSubItem(Item, Client.Host));
					Item.SubItems.Add(new ListViewItem.ListViewSubItem(Item, Client.Stream));
					Item.SubItems.Add(new ListViewItem.ListViewSubItem(Item, Client.Root));
					Item.Selected = (WorkspaceName == Client.Name);
					WorkspaceListView.Items.Add(Item);
				}
			}
		}

		private void UpdateOkButton()
		{
			OkBtn.Enabled = (WorkspaceListView.SelectedItems.Count == 1);
		}

		public static bool ShowModal(IWin32Window Owner, IPerforceSettings Perforce, string WorkspaceName, IServiceProvider ServiceProvider, out string? NewWorkspaceName)
		{
			ModalTask<EnumerateWorkspaces>? Task = PerforceModalTask.Execute(Owner, "Finding workspaces", "Finding workspaces, please wait...", Perforce, EnumerateWorkspaces.RunAsync, ServiceProvider.GetRequiredService<ILogger<EnumerateWorkspaces>>());
			if (Task == null || !Task.Succeeded)
			{
				NewWorkspaceName = null;
				return false;
			}

			SelectWorkspaceWindow SelectWorkspace = new SelectWorkspaceWindow(Task.Result.Info, Task.Result.Clients, WorkspaceName);
			if(SelectWorkspace.ShowDialog(Owner) == DialogResult.OK)
			{
				NewWorkspaceName = SelectWorkspace.WorkspaceName;
				return true;
			}
			else
			{
				NewWorkspaceName = null;
				return false;
			}
		}

		private void OnlyForThisComputer_CheckedChanged(object sender, EventArgs e)
		{
			UpdateListView();
		}

		private void WorkspaceListView_SelectedIndexChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			if(WorkspaceListView.SelectedItems.Count > 0)
			{
				WorkspaceName = WorkspaceListView.SelectedItems[0].Text;
				DialogResult = DialogResult.OK;
				Close();
			}
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
			Close();
		}

		private void WorkspaceListView_MouseDoubleClick(object sender, MouseEventArgs e)
		{
			if(WorkspaceListView.SelectedItems.Count > 0)
			{
				WorkspaceName = WorkspaceListView.SelectedItems[0].Text;
				DialogResult = DialogResult.OK;
				Close();
			}
		}
	}
}
