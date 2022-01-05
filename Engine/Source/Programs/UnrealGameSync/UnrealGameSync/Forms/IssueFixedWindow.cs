// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

#nullable enable

namespace UnrealGameSync
{
	partial class IssueFixedWindow : Form
	{
		class FindChangesWorker : Component
		{
			public delegate void OnCompleteDelegate(string UserName, List<DescribeRecord> Changes);

			IPerforceSettings PerforceSettings;
			SynchronizationContext MainThreadSyncContext;
			AsyncEvent WakeEvent;
			string? RequestedUserName;
			CancellationTokenSource CancellationSource;
			Task BackgroundTask;
			OnCompleteDelegate? OnComplete;
			ILogger Logger;

			public FindChangesWorker(IPerforceSettings PerforceSettings, OnCompleteDelegate? OnComplete, ILogger Logger)
			{
				this.PerforceSettings = PerforceSettings;
				this.MainThreadSyncContext = SynchronizationContext.Current!;
				WakeEvent = new AsyncEvent();
				this.OnComplete = OnComplete;
				this.Logger = Logger;

				CancellationSource = new CancellationTokenSource();
				BackgroundTask = Task.Run(() => DoWork(CancellationSource.Token));
			}

			public void Stop()
			{
				_ = StopAsync();
			}

			Task StopAsync()
			{
				Task StopTask = Task.CompletedTask;
				if(BackgroundTask != null)
				{
					OnComplete = null;

					CancellationSource.Cancel();
					StopTask = BackgroundTask.ContinueWith(_ => CancellationSource.Dispose());

					BackgroundTask = null!;
				}
				return StopTask;
			}

			public void FetchChanges(string UserName)
			{
				RequestedUserName = UserName;
				WakeEvent.Set();
			}

			protected override void Dispose(bool disposing)
			{
				base.Dispose(disposing);

				Stop();
			}

			public async Task DoWork(CancellationToken CancellationToken)
			{
				using IPerforceConnection Perforce = await PerforceConnection.CreateAsync(PerforceSettings, Logger);

				Task WakeTask = WakeEvent.Task;
				Task CancelTask = Task.Delay(-1, CancellationToken);
				for(;;)
				{
					await Task.WhenAny(WakeTask, CancelTask);

					if (CancellationToken.IsCancellationRequested)
					{
						break;
					}

					WakeTask = WakeEvent.Task;

					string? UserName = RequestedUserName;
					if (UserName != null)
					{
						List<ChangesRecord> Changes = await Perforce.GetChangesAsync(ChangesOptions.IncludeTimes, 100, ChangeStatus.Submitted, FileSpecList.Any, CancellationToken);
						List<DescribeRecord> Descriptions = await Perforce.DescribeAsync(Changes.Select(x => x.Number).ToArray(), CancellationToken);

						MainThreadSyncContext.Post(_ => OnComplete?.Invoke(UserName, Descriptions), null);
					}
				}
			}
		}

		IPerforceSettings PerforceSettings;
		int ChangeNumber;
		FindChangesWorker Worker;
		IServiceProvider ServiceProvider;
	
		public IssueFixedWindow(IPerforceSettings PerforceSettings, int InitialChangeNumber, IServiceProvider ServiceProvider)
		{
			InitializeComponent();

			this.PerforceSettings = PerforceSettings;
			this.Worker = new FindChangesWorker(PerforceSettings, PopulateChanges, ServiceProvider.GetRequiredService<ILogger<FindChangesWorker>>());
			this.ServiceProvider = ServiceProvider;
			components!.Add(Worker);

			UserNameTextBox.Text = PerforceSettings.UserName;
			UserNameTextBox.SelectionStart = UserNameTextBox.Text.Length;

			if(InitialChangeNumber != 0)
			{
				if(InitialChangeNumber < 0)
				{
					SpecifyChangeRadioButton.Checked = false;
					SystemicFixRadioButton.Checked = true;
				}
				else
				{
					SpecifyChangeRadioButton.Checked = true;
					ChangeNumberTextBox.Text = InitialChangeNumber.ToString();
				}
			}

			UpdateOkButton();
		}

		protected override void OnLoad(EventArgs e)
		{
			base.OnLoad(e);

			if(SystemicFixRadioButton.Checked)
			{
				SystemicFixRadioButton.Select();
			}
			else if(SpecifyChangeRadioButton.Checked)
			{
				SpecifyChangeRadioButton.Select();
			}
			else if(RecentChangeRadioButton.Checked)
			{
				ChangesListView.Select();
			}

			Worker.FetchChanges(UserNameTextBox.Text);
		}

		public static bool ShowModal(IWin32Window Owner, IPerforceSettings Perforce, IServiceProvider ServiceProvider, ref int FixChangeNumber)
		{
			using(IssueFixedWindow FixedWindow = new IssueFixedWindow(Perforce, FixChangeNumber, ServiceProvider))
			{
				if(FixedWindow.ShowDialog(Owner) == DialogResult.OK)
				{
					FixChangeNumber = FixedWindow.ChangeNumber;
					return true;
				}
				else
				{
					FixChangeNumber = 0;
					return false;
				}
			}
		}

		private void UpdateSelectedChangeAndClose()
		{
			if (TryGetSelectedChange(out ChangeNumber))
			{
				DialogResult = DialogResult.OK;
				Close();
			}
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			UpdateSelectedChangeAndClose();
		}

		private void PopulateChanges(string UserName, List<DescribeRecord> Changes)
		{
			if(!IsDisposed)
			{
				ChangesListView.BeginUpdate();
				ChangesListView.Items.Clear();
				if(Changes != null)
				{
					foreach(DescribeRecord Change in Changes)
					{
						if(Change.Description != null && Change.Description.IndexOf("#ROBOMERGE-SOURCE", 0) == -1)
						{
							string Stream = "";
							if(Change.Files.Count > 0)
							{
								string DepotFile = Change.Files[0].DepotFile;

								int Idx = 0;
								for(int Count = 0; Idx < DepotFile.Length; Idx++)
								{
									if(DepotFile[Idx] == '/' && ++Count >= 4)
									{
										break;
									}
								}

								Stream = DepotFile.Substring(0, Idx);
							}

							ListViewItem Item = new ListViewItem("");
							Item.Tag = Change;
							Item.SubItems.Add(Change.Number.ToString());
							Item.SubItems.Add(Stream);
							Item.SubItems.Add(Change.Description.Replace('\n', ' '));
							ChangesListView.Items.Add(Item);
						}
					}
				}
				ChangesListView.EndUpdate();
			}
		}

		private void ChangesListView_MouseClick(object Sender, MouseEventArgs Args)
		{
			if(Args.Button == MouseButtons.Right)
			{
				ListViewHitTestInfo HitTest = ChangesListView.HitTest(Args.Location);
				if(HitTest.Item != null && HitTest.Item.Tag != null)
				{
					DescribeRecord? Record = HitTest.Item.Tag as DescribeRecord; 
					if(Record != null)
					{
						ChangesListContextMenu.Tag = Record;
						ChangesListContextMenu.Show(ChangesListView, Args.Location);
					}
				}
			}
		}

		private void ChangesListView_MouseDoubleClick(object sender, MouseEventArgs e)
		{
			UpdateSelectedChangeAndClose();
		}

		private void ChangesContextMenu_MoreInfo_Click(object sender, EventArgs e)
		{
			DescribeRecord Record = (DescribeRecord)ChangesListContextMenu.Tag;
			Program.SpawnP4VC(String.Format("{0} change {1}", PerforceSettings.GetArgumentsForExternalProgram(true), Record.Number));
		}

		private void ChangeNumberTextBox_Enter(object sender, EventArgs e)
		{
			SpecifyChangeRadioButton.Checked = true;
			RecentChangeRadioButton.Checked = false;
		}

		private void ChangesListView_Enter(object sender, EventArgs e)
		{
			SpecifyChangeRadioButton.Checked = false;
			RecentChangeRadioButton.Checked = true;
		}

		private void UserNameTextBox_Enter(object sender, EventArgs e)
		{
			SpecifyChangeRadioButton.Checked = false;
			RecentChangeRadioButton.Checked = true;
		}

		private void UserBrowseBtn_Enter(object sender, EventArgs e)
		{
			SpecifyChangeRadioButton.Checked = false;
			RecentChangeRadioButton.Checked = true;
		}

		private void ChangeNumberTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
		}

		private void ChangesListView_ItemSelectionChanged(object sender, ListViewItemSelectionChangedEventArgs e)
		{
			UpdateOkButton();
		}

		private bool TryGetSelectedChange(out int ChangeNumber)
		{
			if(SpecifyChangeRadioButton.Checked)
			{
				return int.TryParse(ChangeNumberTextBox.Text, out ChangeNumber);
			}
			else if(SystemicFixRadioButton.Checked)
			{
				ChangeNumber = -1;
				return true;
			}
			else
			{
				DescribeRecord? Change = (ChangesListView.SelectedItems.Count > 0)? ChangesListView.SelectedItems[0].Tag as DescribeRecord : null;
				if(Change == null)
				{
					ChangeNumber = 0;
					return false;
				}
				else
				{
					ChangeNumber = Change.Number;
					return true;
				}
			}
		}

		private void UpdateOkButton()
		{
			int ChangeNumber;
			OkBtn.Enabled = TryGetSelectedChange(out ChangeNumber);
		}

		private void SpecifyChangeRadioButton_CheckedChanged(object sender, EventArgs e)
		{
			if(SpecifyChangeRadioButton.Checked)
			{
				RecentChangeRadioButton.Checked = false;
				SystemicFixRadioButton.Checked = false;
			}
			UpdateOkButton();
		}

		private void RecentChangeRadioButton_CheckedChanged(object sender, EventArgs e)
		{
			if(RecentChangeRadioButton.Checked)
			{
				SpecifyChangeRadioButton.Checked = false;
				SystemicFixRadioButton.Checked = false;
			}
			UpdateOkButton();
		}

		private void SystemicFixRadioButton_CheckedChanged(object sender, EventArgs e)
		{
			if(SystemicFixRadioButton.Checked)
			{
				RecentChangeRadioButton.Checked = false;
				SpecifyChangeRadioButton.Checked = false;
			}
			UpdateOkButton();
		}

		private void UserBrowseBtn_Click(object sender, EventArgs e)
		{
			string? SelectedUserName;
			if(SelectUserWindow.ShowModal(this, PerforceSettings, ServiceProvider, out SelectedUserName))
			{
				UserNameTextBox.Text = SelectedUserName;
			}
		}

		private void UserNameTextBox_TextChanged(object sender, EventArgs e)
		{
			Worker.FetchChanges(UserNameTextBox.Text);
		}
	}
}
