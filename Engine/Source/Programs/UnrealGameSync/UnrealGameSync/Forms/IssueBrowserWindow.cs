// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	partial class IssueBrowserWindow : Form
	{
		IssueMonitor IssueMonitor;
		IPerforceSettings PerforceSettings;
		TimeSpan? ServerTimeOffset;
		IServiceProvider ServiceProvider;
		ILogger Logger;
		string? CurrentStream;
		int MaxResults = 0;
		int PendingMaxResults = 0;
		string? FilterName;
		Dictionary<string, Func<IssueData, bool>> CustomFilters = new Dictionary<string, Func<IssueData, bool>>();
		List<IssueData> Issues = new List<IssueData>();
		SynchronizationContext MainThreadSynchronizationContext;
		Task? BackgroundTask;
		bool bDisposed;

		public IssueBrowserWindow(IssueMonitor IssueMonitor, IPerforceSettings PerforceSettings, TimeSpan? ServerTimeOffset, IServiceProvider ServiceProvider, string? CurrentStream, Dictionary<string, Func<IssueData, bool>> CustomFilters, string? FilterName)
		{
			this.IssueMonitor = IssueMonitor;
			this.PerforceSettings = PerforceSettings;
			this.ServerTimeOffset = ServerTimeOffset;
			this.ServiceProvider = ServiceProvider;
			this.Logger = ServiceProvider.GetRequiredService<ILogger<IssueBrowserWindow>>();
			this.CurrentStream = CurrentStream;
			this.FilterName = FilterName;
			this.CustomFilters = CustomFilters;
			this.MainThreadSynchronizationContext = SynchronizationContext.Current!;

			IssueMonitor.AddRef();

			InitializeComponent();

			using (Graphics Graphics = Graphics.FromHwnd(IntPtr.Zero))
			{
				float DpiScaleX = Graphics.DpiX / 96.0f;
				foreach (ColumnHeader? Column in IssueListView.Columns)
				{
					if (Column != null)
					{
						Column.Width = (int)(Column.Width * DpiScaleX);
					}
				}
			}

			System.Reflection.PropertyInfo DoubleBufferedProperty = typeof(Control).GetProperty("DoubleBuffered", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)!;
			DoubleBufferedProperty.SetValue(IssueListView, true, null);
		}

		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}

			IssueMonitor.Release();

			bDisposed = true;

			base.Dispose(disposing);
		}

		private void IssueBrowserWindow_Load(object sender, EventArgs e)
		{
			FetchMoreResults();
		}

		private void FetchMoreResults()
		{
			PendingMaxResults = MaxResults + 100;
			CheckToStartBackgroundThread();
		}

		async Task FetchIssues(int NewMaxResults, CancellationToken CancellationToken)
		{
			try
			{
				SortedDictionary<int, IssueData> NewSortedIssues = new SortedDictionary<int, IssueData>();

				List<IssueData> OpenIssues = await RESTApi.GetAsync<List<IssueData>>($"{IssueMonitor.ApiUrl}/api/issues", CancellationToken);
				foreach (IssueData OpenIssue in OpenIssues)
				{
					NewSortedIssues[(int)OpenIssue.Id] = OpenIssue;
				}

				List<IssueData> ResolvedIssues = await RESTApi.GetAsync<List<IssueData>>($"{IssueMonitor.ApiUrl}/api/issues?includeresolved=true&maxresults={NewMaxResults}", CancellationToken);
				foreach (IssueData ResolvedIssue in ResolvedIssues)
				{
					NewSortedIssues[(int)ResolvedIssue.Id] = ResolvedIssue;
				}

				List<IssueData> NewIssues = NewSortedIssues.Values.Reverse().ToList();
				MainThreadSynchronizationContext.Post((o) => { if (!bDisposed) { FetchIssuesSuccess(NewMaxResults, NewIssues); } }, null);
			}
			catch(Exception Ex)
			{
				MainThreadSynchronizationContext.Post((o) => { if (!bDisposed) { FetchIssuesFailure(Ex); } }, null);
			}
		}

		void FetchIssuesSuccess(int NewMaxResults, List<IssueData> NewIssues)
		{
			MaxResults = NewMaxResults;

			// Update the list of project names
			Issues = NewIssues;
			UpdateIssueList();

			BackgroundTask = null;
			CheckToStartBackgroundThread();
		}

		void FetchIssuesFailure(Exception Ex)
		{
			StatusLabel.Text = String.Format("Unable to fetch issues ({0})", Ex.Message);
			BackgroundTask = null;
			CheckToStartBackgroundThread();
		}

		void CheckToStartBackgroundThread()
		{
			if(PendingMaxResults != MaxResults)
			{
				StartBackgroundThread();
			}
		}

		void StartBackgroundThread()
		{
			if(BackgroundTask == null)
			{
				int NewMaxResultsCopy = PendingMaxResults;
				BackgroundTask = Task.Run(() => FetchIssues(NewMaxResultsCopy, CancellationToken.None));
			}
		}

		void UpdateIssueList()
		{
			// Get the time at midnight
			DateTime Now = DateTime.Now;
			DateTime Midnight = (Now - Now.TimeOfDay).ToUniversalTime();

			// Get the regex for the selected filter
			Func<IssueData, bool>? Filter;
			if (String.IsNullOrEmpty(FilterName))
			{
				Filter = x => true;
			}
			else if (!CustomFilters.TryGetValue(FilterName, out Filter))
			{
				Filter = x => x.Streams == null || x.Streams.Any(y => String.Equals(y, FilterName, StringComparison.OrdinalIgnoreCase));
			}

			// Update the table
			int ItemIdx = 0;
			IssueListView.BeginUpdate();
			foreach(IssueData Issue in Issues)
			{
				if(Filter(Issue))
				{
					for(;;)
					{
						if(ItemIdx == IssueListView.Items.Count)
						{
							IssueList_InsertItem(ItemIdx, Issue, Midnight);
							break;
						}

						ListViewItem ExistingItem = IssueListView.Items[ItemIdx];
						IssueData ExistingIssue = (IssueData)ExistingItem.Tag;
						if(ExistingIssue == null || ExistingIssue.Id < Issue.Id)
						{
							IssueList_InsertItem(ItemIdx, Issue, Midnight);
							break;
						}
						else if(ExistingIssue.Id == Issue.Id)
						{
							IssueList_UpdateItem(ExistingItem, Issue, Midnight);
							break;
						}
						else
						{
							IssueListView.Items.RemoveAt(ItemIdx);
							continue;
						}
					}
					ItemIdx++;
				}
			}
			while(ItemIdx < IssueListView.Items.Count)
			{
				IssueListView.Items.RemoveAt(ItemIdx);
			}
			IssueListView.EndUpdate();

			// Update the maximum number of results
			string FilterText = "";
			if(!String.IsNullOrEmpty(FilterName))
			{
				FilterText = String.Format(" matching filter '{0}'", FilterName);
			}
			StatusLabel.Text = (IssueListView.Items.Count == Issues.Count)? String.Format("Showing {0} results{1}.", Issues.Count, FilterText) : String.Format("Showing {0}/{1} results{2}.", IssueListView.Items.Count, Issues.Count, FilterText);
		}

		void IssueList_InsertItem(int ItemIdx, IssueData Issue, DateTime Midnight)
		{
			ListViewItem Item = new ListViewItem("");
			for(int Idx = 0; Idx < IssueListView.Columns.Count - 1; Idx++)
			{
				Item.SubItems.Add("");
			}
			Item.Tag = Issue;
			IssueList_UpdateItem(Item, Issue, Midnight);
			IssueListView.Items.Insert(ItemIdx, Item);
		}

		void IssueList_UpdateItem(ListViewItem Item, IssueData Issue, DateTime Midnight)
		{
			Item.SubItems[IdHeader.Index].Text = Issue.Id.ToString();
			Item.SubItems[CreatedHeader.Index].Text = FormatIssueDateTime(Issue.CreatedAt.ToLocalTime(), Midnight);
			Item.SubItems[ResolvedHeader.Index].Text = Issue.ResolvedAt.HasValue ? FormatIssueDateTime(Issue.ResolvedAt.Value.ToLocalTime(), Midnight) : "Unresolved";
			Item.SubItems[TimeToFixHeader.Index].Text = Issue.ResolvedAt.HasValue ? Utility.FormatDurationMinutes(Issue.ResolvedAt.Value - Issue.CreatedAt) : "-";
			Item.SubItems[OwnerHeader.Index].Text = (Issue.Owner == null) ? "-" : Utility.FormatUserName(Issue.Owner);
			Item.SubItems[DescriptionHeader.Index].Text = Issue.Summary;
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

		public static void Show(Form Owner, IssueMonitor IssueMonitor, IPerforceSettings PerforceSettings, TimeSpan? ServerTimeOffset, IServiceProvider ServiceProvider, string? CurrentStream, Dictionary<string, Func<IssueData, bool>> CustomFilters, string? DefaultFilter)
		{
			IssueBrowserWindow Window = ExistingWindows.FirstOrDefault(x => x.IssueMonitor == IssueMonitor);
			if(Window == null)
			{
				Window = new IssueBrowserWindow(IssueMonitor, PerforceSettings, ServerTimeOffset, ServiceProvider, CurrentStream, CustomFilters, DefaultFilter);
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

			FilterMenu_ShowAll.Checked = (FilterName == null);

			if (CustomFilters.Count > 0)
			{
				foreach (KeyValuePair<string, Func<IssueData, bool>> CustomFilter in CustomFilters.OrderBy(x => x.Key))
				{
					ToolStripMenuItem Item = new ToolStripMenuItem(CustomFilter.Key);
					Item.Checked = (FilterName == CustomFilter.Key);
					Item.Click += (S, E) => { FilterName = CustomFilter.Key; UpdateIssueList(); };
					FilterMenu.Items.Add(Item);
				}
			}

			HashSet<string> Streams = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (IssueData Issue in Issues)
			{
				if (Issue.Streams != null)
				{
					Streams.UnionWith(Issue.Streams);
				}
			}

			if (Streams.Count > 0)
			{
				FilterMenu.Items.Add(new ToolStripSeparator());
				foreach (string Stream in Streams.OrderBy(x => x))
				{
					ToolStripMenuItem Item = new ToolStripMenuItem(Stream);
					Item.Checked = (FilterName == Stream);
					Item.Click += (S, E) => { FilterName = Stream; UpdateIssueList(); };
					FilterMenu.Items.Add(Item);
				}
			}

			FilterMenu.Show(FilterBtn, new Point(FilterBtn.Left, FilterBtn.Bottom));
		}

		private void FilterMenu_ShowAll_Click(object sender, EventArgs e)
		{
			FilterName = null;
			UpdateIssueList();
		}

		private void ShowIssue(IssueData Issue)
		{
			IssueDetailsWindow.Show(Owner, IssueMonitor, PerforceSettings, ServerTimeOffset, Issue, ServiceProvider, CurrentStream);
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

		private void RefreshIssuesTimer_Tick(object sender, EventArgs e)
		{
			StartBackgroundThread();
		}
	}
}
