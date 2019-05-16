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
	partial class IssueBrowserWindow : Form
	{
		IssueMonitor IssueMonitor;
		string ServerAndPort;
		string UserName;
		TextWriter Log;
		string CurrentStream;
		int MaxResults = 0;

		public IssueBrowserWindow(IssueMonitor IssueMonitor, string ServerAndPort, string UserName, TextWriter Log, string CurrentStream)
		{
			this.IssueMonitor = IssueMonitor;
			this.ServerAndPort = ServerAndPort;
			this.UserName = UserName;
			this.Log = Log;
			this.CurrentStream = CurrentStream;

			InitializeComponent();

			System.Reflection.PropertyInfo DoubleBufferedProperty = typeof(Control).GetProperty("DoubleBuffered", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
			DoubleBufferedProperty.SetValue(IssueListView, true, null);
		}

		private void IssueBrowserWindow_Load(object sender, EventArgs e)
		{
			FetchMoreResults();
			UpdateDetailsBtn();
		}

		class QueryIssuesTask : IModalTask
		{
			string ApiUrl;
			int MaxResults;
			public List<IssueData> Issues;

			public QueryIssuesTask(string ApiUrl, int MaxResults)
			{
				this.ApiUrl = ApiUrl;
				this.MaxResults = MaxResults;
			}

			public bool Run(out string ErrorMessage)
			{
				Issues = RESTApi.GET<List<IssueData>>(ApiUrl, String.Format("issues?includeresolved=true&maxresults={0}", MaxResults));

				ErrorMessage = null;
				return true;
			}
		}

		private void FetchMoreResults()
		{
			int NewMaxResults = MaxResults + 100;
			QueryIssuesTask Task = new QueryIssuesTask(IssueMonitor.ApiUrl, NewMaxResults);

			// Execute the task
			string ErrorMessage;
			ModalTaskResult Result = ModalTask.Execute(this, Task, "Querying issues", "Fetching data, please wait...", out ErrorMessage);
			if(Result != ModalTaskResult.Succeeded)
			{
				if(Result != ModalTaskResult.Aborted)
				{
					StatusLabel.Text = "Unable to fetch issues.";
					MessageBox.Show(ErrorMessage);
				}
				return;
			}

			// Get the time at midnight
			DateTime Now = DateTime.Now;
			DateTime Midnight = (Now - Now.TimeOfDay).ToUniversalTime();

			// Fetch the new issues
			IssueListView.BeginUpdate();
			IssueListView.Items.Clear();
			foreach(IssueData Issue in Task.Issues)
			{
				ListViewItem Item = new ListViewItem("");
				Item.SubItems.Add(Issue.Id.ToString());
				Item.SubItems.Add(FormatIssueDateTime(Issue.CreatedAt.ToLocalTime(), Midnight));
				Item.SubItems.Add(Issue.ResolvedAt.HasValue? FormatIssueDateTime(Issue.ResolvedAt.Value.ToLocalTime(), Midnight) : "Unresolved");
				Item.SubItems.Add((Issue.Owner == null)? "-" : Utility.FormatUserName(Issue.Owner));
				Item.SubItems.Add(Issue.Summary);
				Item.Tag = Issue;
				IssueListView.Items.Add(Item);
			}
			IssueListView.EndUpdate();

			// Update the maximum number of results
			StatusLabel.Text = String.Format("Showing {0} results.", Task.Issues.Count);
			MaxResults = NewMaxResults;
		}

		static string FormatIssueDateTime(DateTime DateTime, DateTime Midnight)
		{
			if(DateTime > Midnight)
			{
				return DateTime.ToShortTimeString();
			}
			else
			{
				return DateTime.ToShortDateString();
			}
		}

		public static void ShowModal(IWin32Window Owner, IssueMonitor IssueMonitor, string ServerAndPort, string UserName, TextWriter Log, string CurrentStream)
		{
			IssueBrowserWindow Window = new IssueBrowserWindow(IssueMonitor, ServerAndPort, UserName, Log, CurrentStream);
			Window.ShowDialog(Owner);
		}

		private void FetchMoreResultsLinkLabel_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			FetchMoreResults();
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.OK;
			Close();
		}

		private void IssueListView_SelectedIndexChanged(object sender, EventArgs e)
		{
			UpdateDetailsBtn();
		}

		void UpdateDetailsBtn()
		{
			DetailsBtn.Enabled = (IssueListView.SelectedItems.Count != 0);
		}

		private void IssueListView_MouseDoubleClick(object sender, MouseEventArgs e)
		{
			ListViewHitTestInfo HitTest = IssueListView.HitTest(e.Location);
			if(HitTest.Item != null)
			{
				IssueData Issue = (IssueData)HitTest.Item.Tag;
				ShowIssue(Issue);
			}
		}

		private void DetailsBtn_Click(object sender, EventArgs e)
		{
			foreach(ListViewItem Item in IssueListView.SelectedItems)
			{
				IssueData Issue = (IssueData)Item.Tag;
				ShowIssue(Issue);
				break;
			}
		}

		private void ShowIssue(IssueData Issue)
		{
			Issue.Builds = RESTApi.GET<List<IssueBuildData>>(IssueMonitor.ApiUrl, String.Format("issues/{0}/builds", Issue.Id));
			IssueDetailsWindow.ShowModal(this, IssueMonitor, ServerAndPort, UserName, Issue, Log, CurrentStream);
		}

		private void IssueListView_DrawColumnHeader(object sender, DrawListViewColumnHeaderEventArgs e)
		{
			e.DrawDefault = true;
		}

		private void IssueListView_DrawItem(object sender, DrawListViewItemEventArgs e)
		{
			if (e.Item.Selected)
			{
				IssueListView.DrawSelectedBackground(e.Graphics, e.Bounds);
			}
			else if (e.ItemIndex == IssueListView.HoverItem)
			{
				IssueListView.DrawTrackedBackground(e.Graphics, e.Bounds);
			}
			else if (e.Item.Tag is IssueData)
			{
				IssueData Issue = (IssueData)e.Item.Tag;

				Color BackgroundColor;
				if (Issue.ResolvedAt.HasValue)
				{
					BackgroundColor = SystemColors.Window;//Color.FromArgb(248, 254, 246);
				}
				else if(Issue.FixChange > 0)
				{
					BackgroundColor = Color.FromArgb(245, 245, 245);
				}
				else
				{
					BackgroundColor = Color.FromArgb(254, 248, 246);
				}

				using (SolidBrush Brush = new SolidBrush(BackgroundColor))
				{
					e.Graphics.FillRectangle(Brush, e.Bounds);
				}
			}
			else
			{
				IssueListView.DrawDefaultBackground(e.Graphics, e.Bounds);
			}
		}

		private void IssueListView_DrawSubItem(object sender, DrawListViewSubItemEventArgs e)
		{
			IssueData Issue = (IssueData)e.Item.Tag;
			if (e.ColumnIndex == IconHeader.Index)
			{
				if(!Issue.ResolvedAt.HasValue)
				{
					IssueListView.DrawIcon(e.Graphics, e.Bounds, WorkspaceControl.BadBuildIcon);
				}
				else if (Issue.FixChange > 0)
				{
					IssueListView.DrawIcon(e.Graphics, e.Bounds, WorkspaceControl.MixedBuildIcon);
				}
				else
				{
					IssueListView.DrawIcon(e.Graphics, e.Bounds, WorkspaceControl.GoodBuildIcon);
				}
			}
			else
			{
				IssueListView.DrawNormalSubItem(e);
			}
		}
	}
}
