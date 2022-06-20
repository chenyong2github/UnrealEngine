// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using UnrealGameSync;

namespace UnrealGameSyncLauncher
{
	partial class SettingsWindow : Form
	{
		[DllImport("user32.dll")]
		private static extern IntPtr SendMessage(IntPtr hWnd, int Msg, int wParam, [MarshalAs(UnmanagedType.LPWStr)] string lParam);

		public delegate Task SyncAndRunDelegate(IPerforceConnection Perforce, string? DepotPath, bool bPreview, ILogger LogWriter, CancellationToken CancellationToken);

		const int EM_SETCUEBANNER = 0x1501;

		string? LogText;
		SyncAndRunDelegate SyncAndRun;

		public SettingsWindow(string? Prompt, string? LogText, string? ServerAndPort, string? UserName, string? DepotPath, bool bPreview, SyncAndRunDelegate SyncAndRun)
		{
			InitializeComponent();

			if(Prompt != null)
			{
				this.PromptLabel.Text = Prompt;
			}

			this.LogText = LogText;
			this.ServerTextBox.Text = ServerAndPort ?? String.Empty;
			this.UserNameTextBox.Text = UserName ?? String.Empty;
			this.DepotPathTextBox.Text = DepotPath ?? String.Empty;
			this.UsePreviewBuildCheckBox.Checked = bPreview;
			this.SyncAndRun = SyncAndRun;

			ViewLogBtn.Visible = LogText != null;
		}

		protected override void OnLoad(EventArgs e)
		{
			base.OnLoad(e);

			SendMessage(ServerTextBox.Handle, EM_SETCUEBANNER, 1, "Default Server");
			SendMessage(UserNameTextBox.Handle, EM_SETCUEBANNER, 1, "Default User");
		}

		private void ViewLogBtn_Click(object sender, EventArgs e)
		{
			LogWindow Log = new LogWindow(LogText ?? String.Empty);
			Log.ShowDialog(this);
		}

		private void ConnectBtn_Click(object sender, EventArgs e)
		{
			// Update the settings
			string? ServerAndPort = ServerTextBox.Text.Trim();
			if(ServerAndPort.Length == 0)
			{
				ServerAndPort = null;
			}

			string? UserName = UserNameTextBox.Text.Trim();
			if(UserName.Length == 0)
			{
				UserName = null;
			}

			string? DepotPath = DepotPathTextBox.Text.Trim();
			if(DepotPath.Length == 0)
			{
				DepotPath = null;
			}

			bool bPreview = UsePreviewBuildCheckBox.Checked;

			GlobalPerforceSettings.SaveGlobalPerforceSettings(ServerAndPort, UserName, DepotPath, bPreview);

			PerforceSettings PerforceSettings = new PerforceSettings(PerforceSettings.Default);
			if (!String.IsNullOrEmpty(ServerAndPort))
			{
				PerforceSettings.ServerAndPort = ServerAndPort;
			}
			if (!String.IsNullOrEmpty(UserName))
			{
				PerforceSettings.UserName = UserName;
			}
			PerforceSettings.PreferNativeClient = true;

			// Create the P4 connection
			CaptureLogger Logger = new CaptureLogger();

			// Create the task for connecting to this server
			ModalTask? Task = PerforceModalTask.Execute(this, "Updating", "Checking for updates, please wait...", PerforceSettings, (p, c) => SyncAndRun(p, DepotPath, bPreview, Logger, c), Logger);
			if (Task != null)
			{
				if(Task.Succeeded)
				{
					GlobalPerforceSettings.SaveGlobalPerforceSettings(ServerAndPort, UserName, DepotPath, bPreview);
					DialogResult = DialogResult.OK;
					Close();
				}
				PromptLabel.Text = Task.Error;
			}

			LogText = Logger.Render(Environment.NewLine);
			ViewLogBtn.Visible = true;
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
			Close();
		}
	}
}
