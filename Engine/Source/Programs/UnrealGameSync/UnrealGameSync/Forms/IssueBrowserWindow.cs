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
		TimeSpan? ServerTimeOffset;
		TextWriter Log;
		string CurrentStream;
		int MaxResults = 0;
		string FilterProjectName;
		List<string> ProjectNames = new List<string>();
		List<IssueData> Issues = new List<IssueData>();

		public IssueBrowserWindow(IssueMonitor IssueMonitor, string ServerAndPort, string UserName, TimeSpan? ServerTimeOffset, TextWriter Log, string CurrentStream)
		{
			this.IssueMonitor = IssueMonitor;
			this.ServerAndPort = ServerAndPort;
			this.UserName = UserName;
			this.ServerTimeOffset = ServerTimeOffset;
			this.Log = Log;
			this.CurrentStream = CurrentStream;

			IssueMonitor.AddRef();

			InitializeComponent();

			System.Reflection.PropertyInfo DoubleBufferedProperty = typeof(Control).GetProperty("DoubleBuffered", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
			DoubleBufferedProperty.SetValue(IssueListView, true, null);
		}

		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}

			IssueMonitor.Release();

			base.Dispose(disposing);
		}

		private void IssueBrowserWindow_Load(object sender, EventArgs e)
		{
			FetchMoreResults();
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

			// Update the list of project names
			Issues = Task.Issues;
			ProjectNames = Task.Issues.Select(x => x.Project).Distinct().OrderBy(x => x).ToList();

			// Populate the list control
			UpdateIssueList();
			MaxResults = NewMaxResults;
		}

		void UpdateIssueList()
		{
			// Get the time at midnight
			DateTime Now = DateTime.Now;
			DateTime Midnight = (Now - Now.TimeOfDay).ToUniversalTime();

			// Fetch the new issues
			IssueListView.BeginUpdate();
			IssueListView.Items.Clear();
			foreach(IssueData Issue in Issues)
			{
				if(FilterProjectName == null || Issue.Project == FilterProjectName)
				{
					ListViewItem Item = new ListViewItem("");
					Item.SubItems.Add(Issue.Id.ToString());
					Item.SubItems.Add(Issue.Project);
					Item.SubItems.Add(FormatIssueDateTime(Issue.CreatedAt.ToLocalTime(), Midnight));
					Item.SubItems.Add(Issue.ResolvedAt.HasValue? FormatIssueDateTime(Issue.ResolvedAt.Value.ToLocalTime(), Midnight) : "Unresolved");
					Item.SubItems.Add((Issue.Owner == null)? "-" : Utility.FormatUserName(Issue.Owner));
					Item.SubItems.Add(Issue.Summary);
					Item.Tag = Issue;
					IssueListView.Items.Add(Item);
				}
			}
			IssueListView.EndUpdate();

			// Update the maximum number of results
			StatusLabel.Text = (IssueListView.Items.Count == Issues.Count)? String.Format("Showing {0} results.", Issues.Count) : String.Format("Showing {0}/{1} results.", IssueListView.Items.Count, Issues.Count);
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

		static List<IssueBrowserWindow> ExistingWindows = new List<IssueBrowserWindow>();

		public static void Show(Form Owner, IssueMonitor IssueMonitor, string ServerAndPort, string UserName, TimeSpan? ServerTimeOffset, TextWriter Log, string CurrentStream)
		{
			IssueBrowserWindow Window = ExistingWindows.FirstOrDefault(x => x.IssueMonitor == IssueMonitor);
			if(Window == null)
			{
				Window = new IssueBrowserWindow(IssueMonitor, ServerAndPort, UserName, ServerTimeOffset, Log, CurrentStream);
				Window.Owner = Owner;
				Window.StartPosition = FormStartPosition.Manual;
				Window.Location = new Point(Owner.Location.X + (Owner.Width - Window.Width) / 2, Owner.Location.Y + (Owner.Height - Window.Height) / 2);
				Window.Show(Owner);

				ExistingWindows.Add(Window);
				Window.FormClosed += (E, S) => ExistingWindows.Remove(Window);
			}
			else
			{
				Window.Activate();
			}
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

		private void IssueListView_MouseDoubleClick(object sender, MouseEventArgs e)
		{
			ListViewHitTestInfo HitTest = IssueListView.HitTest(e.Location);
			if(HitTest.Item != null)
			{
				IssueData Issue = (IssueData)HitTest.Item.Tag;
				ShowIssue(Issue);
			}
		}

		private void FilterBtn_Click(object sender, EventArgs e)
		{
			int SeparatorIdx = FilterMenu.Items.IndexOf(FilterMenu_Separator);
			while(FilterMenu.Items.Count > SeparatorIdx + 1)
			{
				FilterMenu.Items.RemoveAt(SeparatorIdx + 1);
			}

			FilterMenu_ShowAll.Checked = (FilterProjectName == null);

			foreach(string ProjectName in ProjectNames)
			{
				ToolStripMenuItem Item = new ToolStripMenuItem(ProjectName);
				Item.Checked = (FilterProjectName == ProjectName);
				Item.Click += (S, E) => { FilterProjectName = ProjectName; UpdateIssueList(); };
				FilterMenu.Items.Add(Item);
			}

			FilterMenu.Show(FilterBtn, new Point(FilterBtn.Left, FilterBtn.Bottom));
		}

		private void FilterMenu_ShowAll_Click(object sender, EventArgs e)
		{
			FilterProjectName = null;
			UpdateIssueList();
		}

		private void ShowIssue(IssueData Issue)
		{
			Issue.Builds = RESTApi.GET<List<IssueBuildData>>(IssueMonitor.ApiUrl, String.Format("issues/{0}/builds", Issue.Id));
			IssueDetailsWindow.Show(Owner, IssueMonitor, ServerAndPort, UserName, ServerTimeOffset, Issue, Log, CurrentStream);
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
				if(!Issue.ResolvedAt.HasValue && Issue.FixChange == 0)
				{
					IssueListView.DrawIcon(e.Graphics, e.Bounds, WorkspaceControl.BadBuildIcon);
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
