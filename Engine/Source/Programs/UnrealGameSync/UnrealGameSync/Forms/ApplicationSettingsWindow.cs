// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Win32;
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

namespace UnrealGameSync
{
	partial class ApplicationSettingsWindow : Form
	{
		class PerforceTestConnectionTask : IPerforceModalTask
		{
			string DepotPath;

			public PerforceTestConnectionTask(string DepotPath)
			{
				this.DepotPath = DepotPath ?? DeploymentSettings.DefaultDepotPath;
			}

			public bool Run(PerforceConnection Perforce, TextWriter Log, out string ErrorMessage)
			{
				string CheckFilePath = String.Format("{0}/Release/UnrealGameSync.exe", DepotPath);

				List<PerforceFileRecord> FileRecords;
				if(!Perforce.FindFiles(CheckFilePath, out FileRecords, Log) || FileRecords.Count == 0)
				{
					ErrorMessage = String.Format("Unable to find {0}", CheckFilePath);
					return false;
				}

				ErrorMessage = null;
				return true;
			}
		}

		string OriginalExecutableFileName;
		UserSettings Settings;
		TextWriter Log;

		string InitialServerAndPort;
		string InitialUserName;
		string InitialDepotPath;
		bool bInitialUnstable;
		int InitialAutomationPortNumber;
		ProtocolHandlerState InitialProtocolHandlerState;

		bool? bRestartUnstable;

		ToolUpdateMonitor ToolUpdateMonitor;

		class ToolItem
		{
			public ToolDefinition Definition { get; }

			public ToolItem(ToolDefinition Definition)
			{
				this.Definition = Definition;
			}

			public override string ToString()
			{
				return Definition.Description;
			}
		}

		private ApplicationSettingsWindow(string DefaultServerAndPort, string DefaultUserName, bool bUnstable, string OriginalExecutableFileName, UserSettings Settings, ToolUpdateMonitor ToolUpdateMonitor, TextWriter Log)
		{
			InitializeComponent();

			this.OriginalExecutableFileName = OriginalExecutableFileName;
			this.Settings = Settings;
			this.ToolUpdateMonitor = ToolUpdateMonitor;
			this.Log = Log;

			Utility.ReadGlobalPerforceSettings(ref InitialServerAndPort, ref InitialUserName, ref InitialDepotPath);
			bInitialUnstable = bUnstable;

			InitialAutomationPortNumber = AutomationServer.GetPortNumber();
			InitialProtocolHandlerState = ProtocolHandlerUtils.GetState();

			this.AutomaticallyRunAtStartupCheckBox.Checked = IsAutomaticallyRunAtStartup();
			this.KeepInTrayCheckBox.Checked = Settings.bKeepInTray;
						
			this.ServerTextBox.Text = InitialServerAndPort;
			this.ServerTextBox.Select(ServerTextBox.TextLength, 0);
			this.ServerTextBox.CueBanner = (DefaultServerAndPort == null)? "Default" : String.Format("Default ({0})", DefaultServerAndPort);

			this.UserNameTextBox.Text = InitialUserName;
			this.UserNameTextBox.Select(UserNameTextBox.TextLength, 0);
			this.UserNameTextBox.CueBanner = (DefaultUserName == null)? "Default" : String.Format("Default ({0})", DefaultUserName);

			this.ParallelSyncThreadsSpinner.Value = Math.Max(Math.Min(Settings.SyncOptions.NumThreads, ParallelSyncThreadsSpinner.Maximum), ParallelSyncThreadsSpinner.Minimum);

			this.DepotPathTextBox.Text = InitialDepotPath;
			this.DepotPathTextBox.Select(DepotPathTextBox.TextLength, 0);
			this.DepotPathTextBox.CueBanner = DeploymentSettings.DefaultDepotPath;

			this.UseUnstableBuildCheckBox.Checked = bUnstable;

			if(InitialAutomationPortNumber > 0)
			{
				this.EnableAutomationCheckBox.Checked = true;
				this.AutomationPortTextBox.Enabled = true;
				this.AutomationPortTextBox.Text = InitialAutomationPortNumber.ToString();
			}
			else
			{
				this.EnableAutomationCheckBox.Checked = false;
				this.AutomationPortTextBox.Enabled = false;
				this.AutomationPortTextBox.Text = AutomationServer.DefaultPortNumber.ToString();
			}

			if(InitialProtocolHandlerState == ProtocolHandlerState.Installed)
			{
				this.EnableProtocolHandlerCheckBox.CheckState = CheckState.Checked;
			}
			else if (InitialProtocolHandlerState == ProtocolHandlerState.NotInstalled)
			{
				this.EnableProtocolHandlerCheckBox.CheckState = CheckState.Unchecked;
			}
			else
			{
				this.EnableProtocolHandlerCheckBox.CheckState = CheckState.Indeterminate;
			}

			List<ToolDefinition> Tools = ToolUpdateMonitor.Tools;
			foreach (ToolDefinition Tool in Tools)
			{
				this.CustomToolsListBox.Items.Add(new ToolItem(Tool), Settings.EnabledTools.Contains(Tool.Id));
			}
		}

		public static bool? ShowModal(IWin32Window Owner, PerforceConnection DefaultConnection, bool bUnstable, string OriginalExecutableFileName, UserSettings Settings, ToolUpdateMonitor ToolUpdateMonitor, TextWriter Log)
		{
			ApplicationSettingsWindow ApplicationSettings = new ApplicationSettingsWindow(DefaultConnection.ServerAndPort, DefaultConnection.UserName, bUnstable, OriginalExecutableFileName, Settings, ToolUpdateMonitor, Log);
			if(ApplicationSettings.ShowDialog() == DialogResult.OK)
			{
				return ApplicationSettings.bRestartUnstable;
			}
			else
			{
				return null;
			}
		}

		private bool IsAutomaticallyRunAtStartup()
		{
			RegistryKey Key = Registry.CurrentUser.OpenSubKey("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run");
			return (Key.GetValue("UnrealGameSync") != null);
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			// Update the settings
			string ServerAndPort = ServerTextBox.Text.Trim();
			if(ServerAndPort.Length == 0)
			{
				ServerAndPort = null;
			}

			string UserName = UserNameTextBox.Text.Trim();
			if(UserName.Length == 0)
			{
				UserName = null;
			}

			string DepotPath = DepotPathTextBox.Text.Trim();
			if(DepotPath.Length == 0 || DepotPath == DeploymentSettings.DefaultDepotPath)
			{
				DepotPath = null;
			}

			bool bUnstable = UseUnstableBuildCheckBox.Checked;


			int AutomationPortNumber;
			if(!EnableAutomationCheckBox.Checked || !int.TryParse(AutomationPortTextBox.Text, out AutomationPortNumber))
			{
				AutomationPortNumber = -1;
			}
			
			if(ServerAndPort != InitialServerAndPort || UserName != InitialUserName || DepotPath != InitialDepotPath || bUnstable != bInitialUnstable || AutomationPortNumber != InitialAutomationPortNumber)
			{
				// Try to log in to the new server, and check the application is there
				if(ServerAndPort != InitialServerAndPort || UserName != InitialUserName || DepotPath != InitialDepotPath)
				{
					string ErrorMessage;
					ModalTaskResult Result = PerforceModalTask.Execute(this, new PerforceConnection(UserName, null, ServerAndPort), new PerforceTestConnectionTask(DepotPath), "Connecting", "Checking connection, please wait...", Log, out ErrorMessage);
					if(Result != ModalTaskResult.Succeeded)
					{
						if(Result == ModalTaskResult.Failed)
						{
							MessageBox.Show(ErrorMessage, "Unable to connect");
						}
						return;
					}
				}

				if(MessageBox.Show("UnrealGameSync must be restarted to apply these settings.\n\nWould you like to restart now?", "Restart Required", MessageBoxButtons.OKCancel) != DialogResult.OK)
				{
					return;
				}

				bRestartUnstable = UseUnstableBuildCheckBox.Checked;
				Utility.SaveGlobalPerforceSettings(ServerAndPort, UserName, DepotPath);
				AutomationServer.SetPortNumber(AutomationPortNumber);
			}

			RegistryKey Key = Registry.CurrentUser.CreateSubKey("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run");
			if (AutomaticallyRunAtStartupCheckBox.Checked)
			{
				Key.SetValue("UnrealGameSync", String.Format("\"{0}\" -RestoreState", OriginalExecutableFileName));
			}
			else
			{
				Key.DeleteValue("UnrealGameSync", false);
			}

			if (Settings.bKeepInTray != KeepInTrayCheckBox.Checked || Settings.SyncOptions.NumThreads != ParallelSyncThreadsSpinner.Value)
			{
				Settings.SyncOptions.NumThreads = (int)ParallelSyncThreadsSpinner.Value;
				Settings.bKeepInTray = KeepInTrayCheckBox.Checked;
				Settings.Save();
			}

			List<Guid> NewEnabledTools = new List<Guid>();
			foreach (ToolItem Item in CustomToolsListBox.CheckedItems)
			{
				NewEnabledTools.Add(Item.Definition.Id);
			}
			if (!NewEnabledTools.SequenceEqual(Settings.EnabledTools))
			{
				Settings.EnabledTools = NewEnabledTools.ToArray();
				Settings.Save();
				ToolUpdateMonitor.UpdateNow();
			}

			if (EnableProtocolHandlerCheckBox.CheckState == CheckState.Checked)
			{
				if (InitialProtocolHandlerState != ProtocolHandlerState.Installed)
				{
					ProtocolHandlerUtils.Install();
				}
			}
			else if (EnableProtocolHandlerCheckBox.CheckState == CheckState.Unchecked)
			{
				if (InitialProtocolHandlerState != ProtocolHandlerState.NotInstalled)
				{
					ProtocolHandlerUtils.Uninstall();
				}
			}

			DialogResult = DialogResult.OK;
			Close();
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
			Close();
		}

		private void EnableAutomationCheckBox_CheckedChanged(object sender, EventArgs e)
		{
			AutomationPortTextBox.Enabled = EnableAutomationCheckBox.Checked;
		}

		private void AdvancedBtn_Click(object sender, EventArgs e)
		{
			PerforceSyncSettingsWindow Window = new PerforceSyncSettingsWindow(Settings);
			Window.ShowDialog();
		}
	}
}
