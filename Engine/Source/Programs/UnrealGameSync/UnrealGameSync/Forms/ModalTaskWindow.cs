// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	partial class ModalTaskWindow : Form
	{
		Task Task;
		CancellationTokenSource CancellationSource;

		public ModalTaskWindow(string InTitle, string InMessage, FormStartPosition InStartPosition, Task InTask, CancellationTokenSource InCancellationSource)//, Func<CancellationToken, Task> InTaskFunc)
		{
			InitializeComponent();

			Text = InTitle;
			MessageLabel.Text = InMessage;
			StartPosition = InStartPosition;
			Task = InTask;
			CancellationSource = InCancellationSource;
		}

		public void ShowAndActivate()
		{
			Show();
			Activate();
		}

		private void ModalTaskWindow_Load(object sender, EventArgs e)
		{
			SynchronizationContext SyncContext = SynchronizationContext.Current!;
			Task.ContinueWith(x => SyncContext.Post(y => Close(), null));
		}

		private void ModalTaskWindow_FormClosing(object sender, FormClosingEventArgs e)
		{
			CancellationSource.Cancel();
			if (!Task.IsCompleted)
			{
				e.Cancel = true;
			}
		}
	}
}
