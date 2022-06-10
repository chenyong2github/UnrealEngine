// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Windows.Forms;

namespace UnrealGameSync
{
	partial class PerforceSyncSettingsWindow : Form
	{
		UserSettings Settings;
		ILogger Logger;

		public PerforceSyncSettingsWindow(UserSettings Settings, ILogger Logger)
		{
			this.Settings = Settings;
			this.Logger = Logger;

			InitializeComponent();
		}

		private void PerforceSettingsWindow_Load(object sender, EventArgs e)
		{
			PerforceSyncOptions SyncOptions = Settings.SyncOptions;
			numericUpDownNumRetries.Value = (SyncOptions.NumRetries > 0) ? SyncOptions.NumRetries : PerforceSyncOptions.DefaultNumRetries;
			numericUpDownTcpBufferSize.Value = (SyncOptions.TcpBufferSize > 0) ? SyncOptions.TcpBufferSize / 1024 : PerforceSyncOptions.DefaultTcpBufferSize / 1024;
			numericUpDownFileBufferSize.Value = (SyncOptions.FileBufferSize > 0) ? SyncOptions.FileBufferSize / 1024 : PerforceSyncOptions.DefaultFileBufferSize / 1024;
			numericUpDownMaxCommandsPerBatch.Value = (SyncOptions.MaxCommandsPerBatch > 0) ? SyncOptions.MaxCommandsPerBatch : PerforceSyncOptions.DefaultMaxCommandsPerBatch;
			numericUpDownMaxSizePerBatch.Value = (SyncOptions.MaxSizePerBatch > 0) ? SyncOptions.MaxSizePerBatch / 1024 / 1024 : PerforceSyncOptions.DefaultMaxSizePerBatch / 1024 / 1024;
			numericUpDownRetriesOnSyncError.Value = (SyncOptions.NumSyncErrorRetries > 0) ? SyncOptions.NumSyncErrorRetries : PerforceSyncOptions.DefaultNumSyncErrorRetries;
		}

		private void OkButton_Click(object sender, EventArgs e)
		{
			Settings.SyncOptions.NumRetries = (int)numericUpDownNumRetries.Value;
			Settings.SyncOptions.TcpBufferSize = (int)numericUpDownTcpBufferSize.Value * 1024;
			Settings.SyncOptions.FileBufferSize = (int)numericUpDownFileBufferSize.Value * 1024;
			Settings.SyncOptions.MaxCommandsPerBatch = (int)numericUpDownMaxCommandsPerBatch.Value;
			Settings.SyncOptions.MaxSizePerBatch = (int)numericUpDownMaxSizePerBatch.Value * 1024 * 1024;
			Settings.SyncOptions.NumSyncErrorRetries = (int)numericUpDownRetriesOnSyncError.Value;
			Settings.Save(Logger);

			DialogResult = System.Windows.Forms.DialogResult.OK;
			Close();
		}

		private void CancButton_Click(object sender, EventArgs e)
		{
			DialogResult = System.Windows.Forms.DialogResult.Cancel;
			Close();
		}

		private void ResetButton_Click(object sender, EventArgs e)
		{
			PerforceSyncOptions SyncOptions = Settings.SyncOptions;
			numericUpDownNumRetries.Value = PerforceSyncOptions.DefaultNumRetries;
			numericUpDownTcpBufferSize.Value = PerforceSyncOptions.DefaultTcpBufferSize / 1024;
			numericUpDownFileBufferSize.Value = PerforceSyncOptions.DefaultFileBufferSize / 1024;
			numericUpDownMaxCommandsPerBatch.Value = PerforceSyncOptions.DefaultMaxCommandsPerBatch;
			numericUpDownMaxSizePerBatch.Value = PerforceSyncOptions.DefaultMaxSizePerBatch / 1024 / 1024;
			numericUpDownRetriesOnSyncError.Value = PerforceSyncOptions.DefaultNumSyncErrorRetries;
		}
	}
}
