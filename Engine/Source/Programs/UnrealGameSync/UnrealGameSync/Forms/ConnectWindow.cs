// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
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
	partial class ConnectWindow : Form
	{
		IPerforceSettings DefaultPerforceSettings;
		string? ServerAndPortOverride;
		string? UserNameOverride;
		IServiceProvider ServiceProvider;

		private ConnectWindow(IPerforceSettings DefaultPerforceSettings, string? ServerAndPortOverride, string? UserNameOverride, IServiceProvider ServiceProvider)
		{
			InitializeComponent();

			this.DefaultPerforceSettings = DefaultPerforceSettings;
			this.ServiceProvider = ServiceProvider;

			if(!String.IsNullOrWhiteSpace(ServerAndPortOverride))
			{
				this.ServerAndPortOverride = ServerAndPortOverride.Trim();
			}
			if(!String.IsNullOrEmpty(UserNameOverride))
			{
				this.UserNameOverride = UserNameOverride.Trim();
			}

			ServerAndPortTextBox.CueBanner = DefaultPerforceSettings.ServerAndPort;
			ServerAndPortTextBox.Text = this.ServerAndPortOverride ?? DefaultPerforceSettings.ServerAndPort;
			UserNameTextBox.CueBanner = DefaultPerforceSettings.UserName;
			UserNameTextBox.Text = this.UserNameOverride ?? DefaultPerforceSettings.UserName;
			UseDefaultConnectionSettings.Checked = this.ServerAndPortOverride == null && this.UserNameOverride == null;

			UpdateEnabledControls();
		}

		public static bool ShowModal(IWin32Window Owner, IPerforceSettings DefaultSettings, ref string? ServerAndPortOverride, ref string? UserNameOverride, IServiceProvider ServiceProvider)
		{
			ConnectWindow Connect = new ConnectWindow(DefaultSettings, ServerAndPortOverride, UserNameOverride, ServiceProvider);
			if(Connect.ShowDialog(Owner) == DialogResult.OK)
			{
				ServerAndPortOverride = Connect.ServerAndPortOverride;
				UserNameOverride = Connect.UserNameOverride;
				return true;
			}
			return false;
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
			Close();
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			if(UseDefaultConnectionSettings.Checked)
			{
				ServerAndPortOverride = null;
				UserNameOverride = null;
			}
			else
			{
				ServerAndPortOverride = ServerAndPortTextBox.Text.Trim();
				if(ServerAndPortOverride.Length == 0)
				{
					ServerAndPortOverride = DefaultPerforceSettings.ServerAndPort;
				}

				UserNameOverride = UserNameTextBox.Text.Trim();
				if(UserNameOverride.Length == 0)
				{
					UserNameOverride = DefaultPerforceSettings.UserName;
				}
			}

			DialogResult = DialogResult.OK;
			Close();
		}

		private void UseCustomSettings_CheckedChanged(object sender, EventArgs e)
		{
			UpdateEnabledControls();
		}

		private void UpdateEnabledControls()
		{
			bool bUseDefaultSettings = UseDefaultConnectionSettings.Checked;

			ServerAndPortLabel.Enabled = !bUseDefaultSettings;
			ServerAndPortTextBox.Enabled = !bUseDefaultSettings;

			UserNameLabel.Enabled = !bUseDefaultSettings;
			UserNameTextBox.Enabled = !bUseDefaultSettings;
		}

		private void BrowseUserBtn_Click(object sender, EventArgs e)
		{
			string? NewUserName;
			if(SelectUserWindow.ShowModal(this, new PerforceSettings(DefaultPerforceSettings) { UserName = UserNameTextBox.Text, ServerAndPort = ServerAndPortTextBox.Text }, ServiceProvider, out NewUserName))
			{
				UserNameTextBox.Text = NewUserName;
			}
		}
	}
}
