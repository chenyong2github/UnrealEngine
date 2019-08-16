// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
		PerforceConnection DefaultConnection;
		string ServerAndPortOverride;
		string UserNameOverride;
		TextWriter Log;

		private ConnectWindow(PerforceConnection DefaultConnection, string ServerAndPortOverride, string UserNameOverride, TextWriter Log)
		{
			InitializeComponent();

			this.DefaultConnection = DefaultConnection;
			this.Log = Log;

			if(!String.IsNullOrWhiteSpace(ServerAndPortOverride))
			{
				this.ServerAndPortOverride = ServerAndPortOverride.Trim();
			}
			if(!String.IsNullOrEmpty(UserNameOverride))
			{
				this.UserNameOverride = UserNameOverride.Trim();
			}

			ServerAndPortTextBox.CueBanner = DefaultConnection.ServerAndPort;
			ServerAndPortTextBox.Text = this.ServerAndPortOverride ?? DefaultConnection.ServerAndPort;
			UserNameTextBox.CueBanner = DefaultConnection.UserName;
			UserNameTextBox.Text = this.UserNameOverride ?? DefaultConnection.UserName;
			UseDefaultConnectionSettings.Checked = this.ServerAndPortOverride == null && this.UserNameOverride == null;

			UpdateEnabledControls();
		}

		public static bool ShowModal(IWin32Window Owner, PerforceConnection DefaultConnection, ref string ServerAndPortOverride, ref string UserNameOverride, TextWriter Log)
		{
			ConnectWindow Connect = new ConnectWindow(DefaultConnection, ServerAndPortOverride, UserNameOverride, Log);
			if(Connect.ShowDialog() == DialogResult.OK)
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
					ServerAndPortOverride = DefaultConnection.ServerAndPort;
				}

				UserNameOverride = UserNameTextBox.Text.Trim();
				if(UserNameOverride.Length == 0)
				{
					UserNameOverride = DefaultConnection.UserName;
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
			string NewUserName;
			if(SelectUserWindow.ShowModal(this, new PerforceConnection(UserNameTextBox.Text, null, ServerAndPortTextBox.Text), Log, out NewUserName))
			{
				UserNameTextBox.Text = NewUserName;
			}
		}
	}
}
