// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
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
		static class PerforceTestConnectionTask
		{
			public static async Task RunAsync(IPerforceConnection Perforce, string DepotPath, CancellationToken CancellationToken)
			{
				string CheckFilePath = String.Format("{0}/Release/UnrealGameSync.exe", DepotPath);

				List<FStatRecord> FileRecords = await Perforce.FStatAsync(CheckFilePath, CancellationToken).ToListAsync(CancellationToken);
				if(FileRecords.Count == 0)
				{
					throw new UserErrorException($"Unable to find {CheckFilePath}");
				}
			}
		}

		string OriginalExecutableFileName;
		IPerforceSettings DefaultPerforceSettings;
		UserSettings Settings;
		ILogger Logger;

		string? InitialServerAndPort;
		string? InitialUserName;
		string? InitialDepotPath;
		bool bInitialPreview;
		int InitialAutomationPortNumber;
		ProtocolHandlerState InitialProtocolHandlerState;

		bool? bRestartPreview;

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

		private ApplicationSettingsWindow(IPerforceSettings DefaultPerforceSettings, bool bPreview, string OriginalExecutableFileName, UserSettings Settings, ToolUpdateMonitor ToolUpdateMonitor, ILogger<ApplicationSettingsWindow> Logger)
		{
			InitializeComponent();

			this.OriginalExecutableFileName = OriginalExecutableFileName;
			this.DefaultPerforceSettings = DefaultPerforceSettings;
			this.Settings = Settings;
			this.ToolUpdateMonitor = ToolUpdateMonitor;
			this.Logger = Logger;

			GlobalPerforceSettings.ReadGlobalPerforceSettings(ref InitialServerAndPort, ref InitialUserName, ref InitialDepotPath, ref bPreview);
			bInitialPreview = bPreview;

			InitialAutomationPortNumber = AutomationServer.GetPortNumber();
			InitialProtocolHandlerState = ProtocolHandlerUtils.GetState();

			this.AutomaticallyRunAtStartupCheckBox.Checked = IsAutomaticallyRunAtStartup();
			this.KeepInTrayCheckBox.Checked = Settings.bKeepInTray;
						
			this.ServerTextBox.Text = InitialServerAndPort;
			this.ServerTextBox.Select(ServerTextBox.TextLength, 0);
			this.ServerTextBox.CueBanner = $"Default ({DefaultPerforceSettings.ServerAndPort})";

			this.UserNameTextBox.Text = InitialUserName;
			this.UserNameTextBox.Select(UserNameTextBox.TextLength, 0);
			this.UserNameTextBox.CueBanner = $"Default ({DefaultPerforceSettings.UserName})";

			this.ParallelSyncThreadsSpinner.Value = Math.Max(Math.Min(Settings.SyncOptions.NumThreads, ParallelSyncThreadsSpinner.Maximum), ParallelSyncThreadsSpinner.Minimum);

			this.DepotPathTextBox.Text = InitialDepotPath;
			this.DepotPathTextBox.Select(DepotPathTextBox.TextLength, 0);
			this.DepotPathTextBox.CueBanner = DeploymentSettings.DefaultDepotPath ?? String.Empty;

			this.UsePreviewBuildCheckBox.Checked = bPreview;

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

		public static bool? ShowModal(IWin32Window Owner, IPerforceSettings DefaultPerforceSettings, bool bPreview, string OriginalExecutableFileName, UserSettings Settings, ToolUpdateMonitor ToolUpdateMonitor, ILogger<ApplicationSettingsWindow> Logger)
		{
			ApplicationSettingsWindow ApplicationSettings = new ApplicationSettingsWindow(DefaultPerforceSettings, bPreview, OriginalExecutableFileName, Settings, ToolUpdateMonitor, Logger);
			if(ApplicationSettings.ShowDialog() == DialogResult.OK)
			{
				return ApplicationSettings.bRestartPreview;
			}
			else
			{
				return null;
			}
		}

		private bool IsAutomaticallyRunAtStartup()
		{
			RegistryKey? Key = Registry.CurrentUser.OpenSubKey("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run");
			return (Key?.GetValue("UnrealGameSync") != null);
		}

		private void OkBtn_Click(object sender, EventArgs e)
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
			if(DepotPath.Length == 0 || DepotPath == DeploymentSettings.DefaultDepotPath)
			{
				DepotPath = null;
			}

			bool bPreview = UsePreviewBuildCheckBox.Checked;


			int AutomationPortNumber;
			if(!EnableAutomationCheckBox.Checked || !int.TryParse(AutomationPortTextBox.Text, out AutomationPortNumber))
			{
				AutomationPortNumber = -1;
			}
			
			if(ServerAndPort != InitialServerAndPort || UserName != InitialUserName || DepotPath != InitialDepotPath || bPreview != bInitialPreview || AutomationPortNumber != InitialAutomationPortNumber)
			{
				// Try to log in to the new server, and check the application is there
				if(ServerAndPort != InitialServerAndPort || UserName != InitialUserName || DepotPath != InitialDepotPath)
				{
					PerforceSettings Settings = Utility.OverridePerforceSettings(DefaultPerforceSettings, ServerAndPort, UserName);

					string? TestDepotPath = DepotPath ?? DeploymentSettings.DefaultDepotPath;
					if (TestDepotPath != null)
					{
						ModalTask? Task = PerforceModalTask.Execute(this, "Checking connection", "Checking connection, please wait...", Settings, (p, c) => PerforceTestConnectionTask.RunAsync(p, TestDepotPath, c), Logger);
						if (Task == null || !Task.Succeeded)
						{
							return;
						}
					}
				}

				if(MessageBox.Show("UnrealGameSync must be restarted to apply these settings.\n\nWould you like to restart now?", "Restart Required", MessageBoxButtons.OKCancel) != DialogResult.OK)
				{
					return;
				}

				bRestartPreview = UsePreviewBuildCheckBox.Checked;
				GlobalPerforceSettings.SaveGlobalPerforceSettings(ServerAndPort, UserName, DepotPath, bPreview);
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
				Settings.Save(Logger);
			}

			List<Guid> NewEnabledTools = new List<Guid>();
			foreach (ToolItem? Item in CustomToolsListBox.CheckedItems)
			{
				if (Item != null)
				{
					NewEnabledTools.Add(Item.Definition.Id);
				}
			}
			if (!NewEnabledTools.SequenceEqual(Settings.EnabledTools))
			{
				Settings.EnabledTools = NewEnabledTools.ToArray();
				Settings.Save(Logger);
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
			PerforceSyncSettingsWindow Window = new PerforceSyncSettingsWindow(Settings, Logger);
			Window.ShowDialog();
		}
	}
}
