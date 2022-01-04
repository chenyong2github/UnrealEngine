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
	partial class SelectUserWindow : Form
	{
		static class EnumerateUsersTask
		{
			public static async Task<List<UsersRecord>> RunAsync(IPerforceConnection Perforce, CancellationToken CancellationToken)
			{
				return await Perforce.GetUsersAsync(UsersOptions.None, -1, CancellationToken);
			}
		}

		private int SelectedUserIndex;
		private List<UsersRecord> Users;
		
		private SelectUserWindow(List<UsersRecord> Users, int SelectedUserIndex)
		{
			InitializeComponent();

			this.SelectedUserIndex = SelectedUserIndex;
			this.Users = Users;

			PopulateList();
			UpdateOkButton();
		}

		private void MoveSelection(int Delta)
		{
			if(UserListView.Items.Count > 0)
			{
				int CurrentIndex = -1;
				if(UserListView.SelectedIndices.Count > 0)
				{
					CurrentIndex = UserListView.SelectedIndices[0];
				}

				int NextIndex = CurrentIndex + Delta;
				if(NextIndex < 0)
				{
					NextIndex = 0;
				}
				else if(NextIndex >= UserListView.Items.Count)
				{
					NextIndex = UserListView.Items.Count - 1;
				}

				if(CurrentIndex != NextIndex)
				{
					if(CurrentIndex != -1)
					{
						UserListView.Items[CurrentIndex].Selected = false;
					}

					UserListView.Items[NextIndex].Selected = true;
					SelectedUserIndex = (int)UserListView.Items[NextIndex].Tag;
				}
			}
		}

		protected override bool ProcessCmdKey(ref Message Msg, Keys KeyData)
		{
			if(KeyData == Keys.Up)
			{
				MoveSelection(-1);
				return true;
			}
			else if(KeyData == Keys.Down)
			{
				MoveSelection(+1);
				return true;
			}
			return base.ProcessCmdKey(ref Msg, KeyData);
		}

		private bool IncludeInFilter(UsersRecord User, string[] FilterWords)
		{
			foreach(string FilterWord in FilterWords)
			{
				if(User.UserName.IndexOf(FilterWord, StringComparison.OrdinalIgnoreCase) == -1 
					&& User.FullName.IndexOf(FilterWord, StringComparison.OrdinalIgnoreCase) == -1
					&& User.Email.IndexOf(FilterWord, StringComparison.OrdinalIgnoreCase) == -1)
				{
					return false;
				}
			}
			return true;
		}

		private void PopulateList()
		{
			UserListView.BeginUpdate();
			UserListView.Items.Clear();

			int SelectedItemIndex = -1;

			string[] Filter = FilterTextBox.Text.Split(new char[]{' '}, StringSplitOptions.RemoveEmptyEntries);
			for(int Idx = 0; Idx < Users.Count; Idx++)
			{
				UsersRecord User = Users[Idx];
				if(IncludeInFilter(User, Filter))
				{
					ListViewItem Item = new ListViewItem(User.UserName);
					Item.SubItems.Add(new ListViewItem.ListViewSubItem(Item, User.FullName));
					Item.SubItems.Add(new ListViewItem.ListViewSubItem(Item, User.Email));
					Item.Tag = (int)Idx;
					UserListView.Items.Add(Item);

					if(SelectedItemIndex == -1 && Idx >= SelectedUserIndex)
					{
						SelectedItemIndex = UserListView.Items.Count - 1;
						Item.Selected = true;
					}
				}
			}

			if(SelectedItemIndex == -1 && UserListView.Items.Count > 0)
			{
				SelectedItemIndex = UserListView.Items.Count - 1;
				UserListView.Items[SelectedItemIndex].Selected = true;
			}

			if(SelectedItemIndex != -1)
			{
				UserListView.EnsureVisible(SelectedItemIndex);
			}

			UserListView.EndUpdate();

			UpdateOkButton();
		}

		public static bool ShowModal(IWin32Window Owner, IPerforceSettings PerforceSettings, IServiceProvider ServiceProvider, [NotNullWhen(true)] out string? SelectedUserName)
		{
			ILogger<SelectUserWindow> Logger = ServiceProvider.GetRequiredService<ILogger<SelectUserWindow>>();

			ModalTask<List<UsersRecord>>? UsersTask = PerforceModalTask.Execute(Owner, "Finding users", "Finding users, please wait...", PerforceSettings, EnumerateUsersTask.RunAsync, Logger);
			if(UsersTask == null || !UsersTask.Succeeded)
			{
				SelectedUserName = null;
				return false;
			}

			SelectUserWindow SelectUser = new SelectUserWindow(UsersTask.Result, 0);
			if(SelectUser.ShowDialog(Owner) == DialogResult.OK)
			{
				SelectedUserName = UsersTask.Result[SelectUser.SelectedUserIndex].UserName;
				return true;
			}
			else
			{
				SelectedUserName = null;
				return false;
			}
		}

		private void FilterTextBox_TextChanged(object sender, EventArgs e)
		{
			PopulateList();
		}

		private void UpdateOkButton()
		{
			OkBtn.Enabled = UserListView.SelectedItems.Count > 0;
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			if(UserListView.SelectedItems.Count > 0)
			{
				SelectedUserIndex = (int)UserListView.SelectedItems[0].Tag;
				DialogResult = DialogResult.OK;
				Close();
			}
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
			Close();
		}

		private void UserListView_SelectedIndexChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
		}
	}
}
