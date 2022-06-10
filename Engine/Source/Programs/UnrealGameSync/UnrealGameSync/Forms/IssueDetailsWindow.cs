// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Data;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	partial class IssueDetailsWindow : Form
	{
		class BuildGroup
		{
			public string JobName { get; }
			public string JobUrl { get; }
			public int Change { get; }
			public IssueBuildOutcome Outcome { get; }
			public IReadOnlyList<IssueBuildData> Builds { get; }

			public BuildGroup(string JobName, string JobUrl, int Change, IssueBuildOutcome Outcome, IReadOnlyList<IssueBuildData> Builds)
			{
				this.JobName = JobName;
				this.JobUrl = JobUrl;
				this.Change = Change;
				this.Outcome = Outcome;
				this.Builds = Builds;
			}
		}

		class PerforceChangeRange
		{
			public bool bExpanded;
			public int MinChange { get; }
			public int MaxChange { get; }
			public List<ChangesRecord>? Changes;
			public string? ErrorMessage;
			public BuildGroup BuildGroup { get; }

			public PerforceChangeRange(int MinChange, int MaxChange, BuildGroup BuildGroup)
			{
				this.MinChange = MinChange;
				this.MaxChange = MaxChange;
				this.BuildGroup = BuildGroup;
			}
		}

		class PerforceChangeDetailsWithDescribeRecord : PerforceChangeDetails
		{
			public DescribeRecord DescribeRecord;

			public PerforceChangeDetailsWithDescribeRecord(DescribeRecord DescribeRecord)
				: base(DescribeRecord)
			{
				this.DescribeRecord = DescribeRecord;
			}
		}

		class PerforceWorkerTask : IDisposable
		{
			readonly IPerforceSettings PerforceSettings;
			readonly string Filter;
			Action<PerforceChangeRange>? OnUpdateChanges;
			Action<ChangesRecord>? OnUpdateChangeMetadata;
			readonly ILogger Logger;
			readonly object LockObject = new object();
			readonly SynchronizationContext SynchronizationContext;
			Task? WorkerTask;
			CancellationTokenSource CancellationSource;
			AsyncEvent RefreshEvent;
			List<PerforceChangeRange> Requests = new List<PerforceChangeRange>();
			Dictionary<int, PerforceChangeDetailsWithDescribeRecord> ChangeNumberToDetails = new Dictionary<int, PerforceChangeDetailsWithDescribeRecord>();

			public PerforceWorkerTask(IPerforceSettings PerforceSettings, string Filter, Action<PerforceChangeRange>? OnUpdateChanges, Action<ChangesRecord>? OnUpdateChangeMetadata, ILogger Logger)
			{
				this.PerforceSettings = PerforceSettings;
				this.Filter = Filter;
				this.OnUpdateChanges = OnUpdateChanges;
				this.OnUpdateChangeMetadata = OnUpdateChangeMetadata;
				this.Logger = Logger;
				this.SynchronizationContext = SynchronizationContext.Current!;

				RefreshEvent = new AsyncEvent();
				CancellationSource = new CancellationTokenSource();
				WorkerTask = Task.Run(() => RunAsync(CancellationSource.Token));
			}

			public void AddRequest(PerforceChangeRange Range)
			{
				lock(LockObject)
				{
					Requests.Add(Range);
				}
				RefreshEvent.Set();
			}

			public void Dispose()
			{
				if (WorkerTask != null)
				{
					OnUpdateChanges = null;
					OnUpdateChangeMetadata = null;

					CancellationSource.Cancel();

					WorkerTask.ContinueWith(x => CancellationSource.Dispose());
					WorkerTask = null;
				}
			}

			async Task RunAsync(CancellationToken CancellationToken)
			{
				using IPerforceConnection Perforce = await PerforceConnection.CreateAsync(PerforceSettings, Logger);

				List<PerforceChangeRange> CompletedRequests = new List<PerforceChangeRange>(); 
				while(!CancellationToken.IsCancellationRequested)
				{
					Task RefreshTask = RefreshEvent.Task;

					// Check if there's a request in the queue
					PerforceChangeRange? NextRequest = null;
					lock(LockObject)
					{
						if(Requests.Count > 0)
						{
							NextRequest = Requests[0];
							Requests.RemoveAt(0);
						}
					}

					// Process the request
					if(NextRequest != null)
					{
						string RangeFilter = String.Format("{0}@{1},{2}", Filter, NextRequest.MinChange, (NextRequest.MaxChange == -1)? "now" : NextRequest.MaxChange.ToString());

						PerforceResponseList<ChangesRecord> NewChanges = await Perforce.TryGetChangesAsync(ChangesOptions.None, -1, ChangeStatus.Submitted, RangeFilter, CancellationToken);
						if(NewChanges.Succeeded)
						{
							NextRequest.Changes = NewChanges.Data;
							CompletedRequests.Add(NextRequest);
						}
						else
						{
							NextRequest.ErrorMessage = "Unable to fetch changes. Check your connection settings and try again.";
						}

						SynchronizationContext.Post(x => OnUpdateChanges?.Invoke(NextRequest), null);
						continue;
					}

					// Figure out which changes to fetch
					List<ChangesRecord> DescribeChanges;
					lock(LockObject)
					{
						DescribeChanges = CompletedRequests.SelectMany(x => x.Changes).Where(x => !ChangeNumberToDetails.ContainsKey(x.Number)).ToList();
					}

					// Fetch info on each individual change
					foreach(ChangesRecord DescribeChange in DescribeChanges)
					{
						lock(LockObject)
						{
							if(CancellationToken.IsCancellationRequested || Requests.Count > 0)
							{
								break;
							}
						}

						PerforceResponse<DescribeRecord> Response = await Perforce.TryDescribeAsync(DescribeChange.Number);
						if (Response.Succeeded)
						{
							DescribeRecord Record = Response.Data;
							lock(LockObject)
							{
								ChangeNumberToDetails[Record.Number] = new PerforceChangeDetailsWithDescribeRecord(Record);
							}
						}

						SynchronizationContext.Post(x => OnUpdateChangeMetadata?.Invoke(DescribeChange), null);
					}

					// Wait for something to change
					using (IDisposable _ = CancellationToken.Register(() => RefreshEvent.Set()))
					{
						await RefreshTask;
					}
				}
			}

			public bool TryGetChangeDetails(int ChangeNumber, [NotNullWhen(true)] out PerforceChangeDetailsWithDescribeRecord? Details)
			{
				lock(LockObject)
				{
					return ChangeNumberToDetails.TryGetValue(ChangeNumber, out Details);
				}
			}
		}

		class BadgeInfo
		{
			public string Label;
			public Color Color;

			public BadgeInfo(string Label, Color Color)
			{
				this.Label = Label;
				this.Color = Color;
			}
		}

		class ExpandRangeStatusElement : StatusElement
		{
			string Text;
			Action LinkAction;

			public ExpandRangeStatusElement(string Text, Action InLinkAction)
			{
				this.Text = Text;
				this.LinkAction = InLinkAction;
				Cursor = NativeCursors.Hand;
			}

			public override void OnClick(Point Location)
			{
				LinkAction();
			}

			public override Size Measure(Graphics Graphics, StatusElementResources Resources)
			{
				return TextRenderer.MeasureText(Graphics, Text, Resources.FindOrAddFont(FontStyle.Regular), new Size(int.MaxValue, int.MaxValue), TextFormatFlags.NoPadding);
			}

			public override void Draw(Graphics Graphics, StatusElementResources Resources)
			{
				Color TextColor = Color.Gray;
				FontStyle Style = FontStyle.Italic;
				if(bMouseDown)
				{
					TextColor = Color.FromArgb(TextColor.B / 2, TextColor.G / 2, TextColor.R);
					Style |= FontStyle.Underline;
				}
				else if(bMouseOver)
				{
					TextColor = Color.FromArgb(TextColor.B, TextColor.G, TextColor.R);
					Style |= FontStyle.Underline;
				}
				TextRenderer.DrawText(Graphics, Text, Resources.FindOrAddFont(Style), Bounds.Location, TextColor, TextFormatFlags.NoPadding);
			}
		}

		IssueMonitor IssueMonitor;
		IssueData Issue;
		List<IssueBuildData> IssueBuilds;
		List<IssueDiagnosticData> Diagnostics;
		IPerforceSettings PerforceSettings;
		TimeSpan? ServerTimeOffset;
		PerforceWorkerTask? PerforceWorker;
		IServiceProvider ServiceProvider;
		SynchronizationContext MainThreadSynchronizationContext;
		string? SelectedStream;
		List<PerforceChangeRange> SelectedStreamRanges = new List<PerforceChangeRange>();
		bool bIsDisposing;
		Font? BoldFont;
		ChangesRecord? ContextMenuChange;
		string? LastOwner;
		string? LastDetailsText;
		System.Windows.Forms.Timer? UpdateTimer;
		StatusElementResources StatusElementResources;

		IssueDetailsWindow(IssueMonitor IssueMonitor, IssueData Issue, List<IssueBuildData> IssueBuilds, List<IssueDiagnosticData> Diagnostics, IPerforceSettings PerforceSettings, TimeSpan? ServerTimeOffset, IServiceProvider ServiceProvider, string? CurrentStream)
		{
			this.IssueMonitor = IssueMonitor;
			this.Issue = Issue;
			this.IssueBuilds = IssueBuilds;
			this.Diagnostics = Diagnostics;
			this.PerforceSettings = PerforceSettings;
			this.ServerTimeOffset = ServerTimeOffset;
			this.ServiceProvider = ServiceProvider;

			IssueMonitor.AddRef();

			MainThreadSynchronizationContext = SynchronizationContext.Current!;

			InitializeComponent();

			this.Text = String.Format("Issue {0}", Issue.Id);
			this.StatusElementResources = new StatusElementResources(BuildListView.Font);
			base.Disposed += IssueDetailsWindow_Disposed;

			IssueMonitor.OnIssuesChanged += OnUpdateIssuesAsync;
			IssueMonitor.StartTracking(Issue.Id);

			BuildListView.SmallImageList = new ImageList(){ ImageSize = new Size(1, 20) };

			System.Reflection.PropertyInfo DoubleBufferedProperty = typeof(Control).GetProperty("DoubleBuffered", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)!;
			DoubleBufferedProperty.SetValue(BuildListView, true, null); 

			using(Graphics Graphics = Graphics.FromHwnd(IntPtr.Zero))
			{
				float DpiScaleX = Graphics.DpiX / 96.0f;
				foreach(ColumnHeader? Column in BuildListView.Columns)
				{
					if (Column != null)
					{
						Column.Width = (int)(Column.Width * DpiScaleX);
					}
				}
			}

			int SelectIdx = 0;
			foreach(string Stream in IssueBuilds.Select(x => x.Stream).Distinct().OrderBy(x => x))
			{
				StreamComboBox.Items.Add(Stream);
				if(Stream == CurrentStream)
				{
					SelectIdx = StreamComboBox.Items.Count - 1;
				}
			}
			if(StreamComboBox.Items.Count > 0)
			{
				StreamComboBox.SelectedIndex = SelectIdx;
			}

			FilterTypeComboBox.SelectedIndex = 0;

			CreateWorker();

			UpdateCurrentIssue();
		}

		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}

			if (IssueMonitor != null)
			{
				IssueMonitor.StopTracking(Issue.Id);
				IssueMonitor.OnIssuesChanged -= OnUpdateIssuesAsync;
				IssueMonitor.Release();
				IssueMonitor = null!;
			}

			base.Dispose(disposing);
		}

		private void StartUpdateTimer()
		{
			if(UpdateTimer == null)
			{
				UpdateTimer = new System.Windows.Forms.Timer();
				UpdateTimer.Interval = 100;
				UpdateTimer.Tick += UpdateTimer_Tick;
				UpdateTimer.Start();

				components.Add(UpdateTimer);
			}
		}

		private void StopUpdateTimer()
		{
			if(UpdateTimer != null)
			{
				components.Remove(UpdateTimer);

				UpdateTimer.Dispose();
				UpdateTimer = null;
			}
		}

		private void UpdateTimer_Tick(object? sender, EventArgs e)
		{
			UpdateBuildList();
			StopUpdateTimer();
		}

		void UpdateSummaryTextIfChanged(Label Label, string NewText)
		{
			if (Label.Text != NewText)
			{
				Label.Text = NewText;
			}
		}

		void UpdateSummaryTextIfChanged(TextBox TextBox, string NewText)
		{
			if(TextBox.Text != NewText)
			{
				TextBox.Text = NewText;
				TextBox.SelectionLength = 0;
				TextBox.SelectionStart = NewText.Length;
			}
		}

		static void AppendEscapedRtf(StringBuilder Result, string Text)
		{
			for (int Idx = 0; Idx < Text.Length; Idx++)
			{
				char Character = Text[Idx];
				if(Character == '\n')
				{
					Result.Append(@"\line");
				}
				else if(Character >= 0x20 && Character <= 0x7f)
				{
					if(Character == '\\' || Character == '{' || Character == '}')
					{
						Result.Append('\\');
					}
					Result.Append(Character);
				}
				else
				{
					Result.AppendFormat("\\u{0}?", (int)Character);
				}
			}
		}

		static void AppendHyperlink(StringBuilder RichText, string Label, string Url)
		{
			RichText.Append(@"{\field");
			RichText.Append(@"{\*\fldinst");
			RichText.AppendFormat("{{ HYPERLINK \"{0}\" }}", Url);
			RichText.Append(@"}");
			RichText.Append(@"{\fldrslt ");
			AppendEscapedRtf(RichText, Label);
			RichText.Append(@"}");
			RichText.Append(@"}");
		}

		static string CreateRichTextErrors(IssueData Issue, List<IssueBuildData> IssueBuilds, List<IssueDiagnosticData> Diagnostics)
		{
			StringBuilder RichText = new StringBuilder();

			RichText.AppendLine(@"{\rtf1\ansi");
			RichText.AppendLine(@"{\fonttbl{\f0\fnil\fcharset0 Arial;}{\f1\fnil\fcharset0 Courier New;}{\f2\fnil\fcharset0 Calibri;}}");
			RichText.AppendLine(@"{\colortbl;\red192\green80\blue77;\red0\green0\blue0;}");

			bool bFirst = true;
			foreach (IGrouping<long, IssueDiagnosticData> Group in Diagnostics.GroupBy(x => x.BuildId ?? -1))
			{
				// Step 'Foo'
				IssueBuildData Build = IssueBuilds.FirstOrDefault(x => x.Id == Group.Key);
				if (Build != null)
				{
					RichText.Append(@"\pard");   // Paragraph default
					RichText.Append(@"\cf2");    // Foreground color
					RichText.Append(@"\b1");     // Bold
					RichText.Append(@"\f0");     // Font
					RichText.Append(@"\fs16");   // Font size
					if (bFirst)
					{
						RichText.Append(@"\sb100"); // Space before
					}
					else
					{
						RichText.Append(@"\sb300"); // Space before
					}
					RichText.Append(@" In step '\ul1");
					AppendHyperlink(RichText, Build.JobStepName, Build.JobStepUrl);
					RichText.Append(@"\ul0':");

					RichText.AppendLine(@"\par");
				}

				IssueDiagnosticData[] DiagnosticsArray = Group.ToArray();
				for(int Idx = 0; Idx < DiagnosticsArray.Length; Idx++)
				{
					IssueDiagnosticData Diagnostic = DiagnosticsArray[Idx];

					// Error X/Y:
					RichText.Append(@"\pard");   // Paragraph default
					RichText.Append(@"\cf1");    // Foreground color
					RichText.Append(@"\b1");     // Bold
					RichText.Append(@"\f0");     // Font
					RichText.Append(@"\fs16");   // Font size
					RichText.Append(@"\fi50");   // First line indent
					RichText.Append(@"\li50");   // Other line indent
					RichText.Append(@"\sb100");  // Space before
					RichText.Append(@"\sa50");   // Space after
					RichText.Append(@" ");

					RichText.Append(@"\ul1");
					AppendHyperlink(RichText, String.Format("Error {0}/{1}", Idx + 1, DiagnosticsArray.Length), Diagnostic.Url);
					RichText.Append(@"\ul0");

					RichText.AppendLine(@"\par");

					// Error text
					foreach(string Line in Diagnostic.Message.TrimEnd().Split('\n'))
					{
						RichText.Append(@"\pard");   // Paragraph default
						RichText.Append(@"\cf0");    // Foreground color
						RichText.Append(@"\b0");     // Bold
						RichText.Append(@"\f1");     // Font
						RichText.Append(@"\fs16");   // Font size 16
						RichText.Append(@"\fi150");  // First line indent
						RichText.Append(@"\li150");  // Other line indent
						AppendEscapedRtf(RichText, Line);
						RichText.Append(@"\par");
					}
				}

				bFirst = false;
			}

			RichText.AppendLine("}");
			return RichText.ToString();
		}

		void UpdateCurrentIssue()
		{
			UpdateSummaryTextIfChanged(SummaryTextBox, Issue.Summary.ToString());

			IssueBuildData FirstFailingBuild = IssueBuilds.FirstOrDefault(x => x.ErrorUrl != null);
			BuildLinkLabel.Text = (FirstFailingBuild != null)? FirstFailingBuild.JobName : "Unknown";

			StringBuilder Status = new StringBuilder();
			if(IssueMonitor.HasPendingUpdate())
			{
				Status.Append("Updating...");
			}
			else if(Issue.FixChange != 0)
			{
				if(Issue.FixChange < 0)
				{
					if(Issue.ResolvedAt.HasValue)
					{
						Status.AppendFormat("Closed as systemic issue.", Issue.FixChange);
					}
					else
					{
						Status.AppendFormat("Fixed as systemic issue (pending verification).", Issue.FixChange);
					}
				}
				else
				{
					if(Issue.ResolvedAt.HasValue)
					{
						Status.AppendFormat("Closed. Fixed in CL {0}.", Issue.FixChange);
					}
					else
					{
						Status.AppendFormat("Fixed in CL {0} (pending verification)", Issue.FixChange);
					}
				}
			}
			else if(Issue.ResolvedAt.HasValue)
			{
				Status.Append("Resolved");
			}
			else if(Issue.Owner == null)
			{
				Status.Append("Currently unassigned");
			}
			else
			{
				Status.Append(Utility.FormatUserName(Issue.Owner));
				if(Issue.NominatedBy != null)
				{
					Status.AppendFormat(" nominated by {0}", Utility.FormatUserName(Issue.NominatedBy));
				}
				if(Issue.AcknowledgedAt.HasValue)
				{
					Status.AppendFormat(" (acknowledged {0})", Utility.FormatRecentDateTime(Issue.AcknowledgedAt.Value.ToLocalTime()));
				}
				else
				{
					Status.Append(" (not acknowledged)");
				}
			}
			UpdateSummaryTextIfChanged(StatusTextBox, Status.ToString());

			StringBuilder OpenSince = new StringBuilder();
			OpenSince.Append(Utility.FormatRecentDateTime(Issue.CreatedAt.ToLocalTime()));
			if(OpenSince.Length > 0)
			{
				OpenSince[0] = Char.ToUpper(OpenSince[0]);
			}
			OpenSince.AppendFormat(" ({0})", Utility.FormatDurationMinutes(Issue.RetrievedAt - Issue.CreatedAt));
			UpdateSummaryTextIfChanged(OpenSinceTextBox, OpenSince.ToString());

			if(LastOwner != Issue.Owner)
			{
				LastOwner = Issue.Owner;
				BuildListView.Invalidate();
			}

			UpdateSummaryTextIfChanged(StepNamesTextBox, String.Join(", ", IssueBuilds.Select(x => x.JobStepName).Distinct().OrderBy(x => x)));
			UpdateSummaryTextIfChanged(StreamNamesTextBox, String.Join(", ", IssueBuilds.Select(x => x.Stream).Distinct().OrderBy(x => x)));

			string RtfText = CreateRichTextErrors(Issue, IssueBuilds, Diagnostics);
			if (LastDetailsText != RtfText)
			{
				using (MemoryStream Stream = new MemoryStream(Encoding.UTF8.GetBytes(RtfText), false))
				{
					DetailsTextBox.LoadFile(Stream, RichTextBoxStreamType.RichText);
					DetailsTextBox.Select(0, 0);
				}
				LastDetailsText = RtfText;
			}

			if(Issue.FixChange == 0)
			{
				MarkFixedBtn.Text = "Mark Fixed...";
			}
			else
			{
				MarkFixedBtn.Text = "Reopen";
			}
		}

		private void IssueDetailsWindow_Disposed(object? sender, EventArgs e)
		{
			bIsDisposing = true;
			DestroyWorker();
		}

		void FetchAllBuildChanges()
		{
			foreach(PerforceChangeRange Range in SelectedStreamRanges)
			{
				FetchBuildChanges(Range);
			}
		}

		void FetchBuildChanges(PerforceChangeRange Range)
		{
			if(!Range.bExpanded)
			{
				Range.bExpanded = true;
				PerforceWorker!.AddRequest(Range);
				UpdateBuildList();
			}
		}

		bool FilterMatch(Regex FilterRegex, ChangesRecord Summary)
		{
			if(FilterRegex.IsMatch(Summary.User))
			{
				return true;
			}
			if(FilterRegex.IsMatch(Summary.Description))
			{
				return true;
			}
			return false;
		}

		bool FilterMatch(Regex FilterRegex, DescribeRecord DescribeRecord)
		{
			if(FilterRegex.IsMatch(DescribeRecord.User))
			{
				return true;
			}
			if(FilterRegex.IsMatch(DescribeRecord.Description))
			{
				return true;
			}
			if(DescribeRecord.Files.Any(x => FilterRegex.IsMatch(x.DepotFile)))
			{
				return true;
			}
			return false;
		}

		void UpdateBuildList()
		{
			int NumNewItems = 0;
			BuildListView.BeginUpdate();

			// Capture the initial selection
			object? PrevSelection = null;
			if(BuildListView.SelectedItems.Count > 0)
			{
				PrevSelection = BuildListView.SelectedItems[0].Tag;
			}

			// Get all the search terms
			string[] FilterTerms = FilterTextBox.Text.Split(new char[]{ ' ' }, StringSplitOptions.RemoveEmptyEntries);

			// Build a regex from each filter term
			List<Regex> FilterRegexes = new List<Regex>();
			foreach(string FilterTerm in FilterTerms)
			{
				string RegexText = Regex.Escape(FilterTerm);
				RegexText = RegexText.Replace("\\?", ".");
				RegexText = RegexText.Replace("\\*", "[^\\\\/]*");
				RegexText = RegexText.Replace("\\.\\.\\.", ".*");
				FilterRegexes.Add(new Regex(RegexText, RegexOptions.IgnoreCase));
			}

			// Get the filter type
			bool bOnlyShowCodeChanges = (FilterTypeComboBox.SelectedIndex == 1);
			bool bOnlyShowContentChanges = (FilterTypeComboBox.SelectedIndex == 2);

			// Create rows for all the ranges
			foreach(PerforceChangeRange Range in SelectedStreamRanges)
			{
				if(Range.bExpanded)
				{
					if(Range.Changes == null)
					{
						ListViewItem FetchingItem = new ListViewItem("");
						FetchingItem.Tag = Range;
						FetchingItem.SubItems.Add("");
						FetchingItem.SubItems.Add("");

						StatusLineListViewWidget FetchingWidget = new StatusLineListViewWidget(FetchingItem, StatusElementResources);
						FetchingWidget.HorizontalAlignment = HorizontalAlignment.Left;
						if(Range.ErrorMessage != null)
						{
							FetchingWidget.Line.AddText(Range.ErrorMessage, Color.Gray, FontStyle.Italic);
						}
						else
						{
							FetchingWidget.Line.AddText("Fetching changes, please wait...", Color.Gray, FontStyle.Italic);
						}
			
						FetchingItem.SubItems.Add(new ListViewItem.ListViewSubItem(FetchingItem, ""){ Tag = FetchingWidget });
						FetchingItem.SubItems.Add(new ListViewItem.ListViewSubItem(FetchingItem, ""){ Tag = FetchingWidget });

						BuildListView.Items.Insert(NumNewItems++, FetchingItem);
					}
					else
					{
						foreach(ChangesRecord Change in Range.Changes)
						{
							PerforceChangeDetailsWithDescribeRecord? Details;
							PerforceWorker!.TryGetChangeDetails(Change.Number, out Details);

							if(FilterRegexes.Count > 0 || bOnlyShowCodeChanges || bOnlyShowContentChanges)
							{
								if(Details == null)
								{
									if(bOnlyShowCodeChanges || bOnlyShowCodeChanges)
									{
										continue;
									}
									if(FilterRegexes.Any(x => !FilterMatch(x, Change)))
									{
										continue;
									}
								}
								else
								{
									if(bOnlyShowCodeChanges && !Details.bContainsCode)
									{
										continue;
									}
									if(bOnlyShowContentChanges && !Details.bContainsContent)
									{
										continue;
									}
									if(FilterRegexes.Any(x => !FilterMatch(x, Details.DescribeRecord)))
									{
										continue;
									}
								}
							}

							ListViewItem Item = new ListViewItem("");
							Item.Tag = Change;

							StatusLineListViewWidget TypeWidget = new StatusLineListViewWidget(Item, StatusElementResources);
							UpdateChangeTypeWidget(TypeWidget, Details);
							Item.SubItems.Add(new ListViewItem.ListViewSubItem(Item, "") { Tag = TypeWidget });

							Item.SubItems.Add(Change.Number.ToString());

							DateTime DisplayTime;
							if (!ServerTimeOffset.HasValue)
							{
								DisplayTime = Change.Time.ToLocalTime();
							}
							else
							{
								DisplayTime = new DateTime(Change.Time.Ticks + ServerTimeOffset.Value.Ticks, DateTimeKind.Local);
							}

							Item.SubItems.Add(DisplayTime.ToString("h\\.mmtt"));

							Item.SubItems.Add(WorkspaceControl.FormatUserName(Change.User));

							Item.SubItems.Add(Change.Description);
							BuildListView.Items.Insert(NumNewItems++, Item);
						}
					}
				}
				/*
				else
				{
					ListViewItem RangeItem = new ListViewItem("");
					RangeItem.Tag = Range;

					StatusLineListViewWidget RangeWidget = new StatusLineListViewWidget(RangeItem, StatusElementResources);
					RangeWidget.Line.AddText("-", Color.Gray);//.AddBadge("...", Color.LightGray, () => FetchBuildChanges(RangeItem));
					RangeItem.SubItems.Add(new ListViewItem.ListViewSubItem(RangeItem, ""){ Tag = RangeWidget });

					StatusLineListViewWidget RangeWidget2 = new StatusLineListViewWidget(RangeItem, StatusElementResources);
					RangeWidget2.Line.AddText("-", Color.Gray);//.AddBadge("...", Color.LightGray, () => FetchBuildChanges(RangeItem));
					RangeItem.SubItems.Add(new ListViewItem.ListViewSubItem(RangeItem, "-"){ Tag = RangeWidget2 });

					StatusLineListViewWidget TipWidget = new StatusLineListViewWidget(RangeItem, StatusElementResources);
					TipWidget.HorizontalAlignment = HorizontalAlignment.Left;
					TipWidget.Line.Add(new ExpandRangeStatusElement("Click to show changes...", () => FetchAllBuildChanges()));

					RangeItem.SubItems.Add(new ListViewItem.ListViewSubItem(RangeItem, ""){ Tag = TipWidget });
					RangeItem.SubItems.Add(new ListViewItem.ListViewSubItem(RangeItem, ""){ Tag = TipWidget });

					BuildListView.Items.Insert(NumNewItems++, RangeItem);
				}
				*/
				ListViewItem BuildItem = new ListViewItem("");
				BuildItem.Tag = Range.BuildGroup;
				BuildItem.SubItems.Add("");
				BuildItem.SubItems.Add(Range.BuildGroup.Change.ToString());
				BuildItem.SubItems.Add("");

				StatusLineListViewWidget BuildWidget = new StatusLineListViewWidget(BuildItem, StatusElementResources);
				BuildWidget.HorizontalAlignment = HorizontalAlignment.Left;

				BuildWidget.Line.AddLink(Range.BuildGroup.JobName, FontStyle.Underline, () => Utility.OpenUrl(Range.BuildGroup.JobUrl));
				BuildItem.SubItems.Add(new ListViewItem.ListViewSubItem(BuildItem, ""){ Tag = BuildWidget });
				BuildItem.SubItems.Add(new ListViewItem.ListViewSubItem(BuildItem, ""){ Tag = BuildWidget });

				BuildListView.Items.Insert(NumNewItems++, BuildItem);
			}
			
			// Re-select the original item
			for(int Idx = 0; Idx < BuildListView.Items.Count; Idx++)
			{
				if(BuildListView.Items[Idx].Tag == PrevSelection)
				{
					BuildListView.Items[Idx].Selected = true;
				}
			}

			// Remove all the items we no longer need
			while(BuildListView.Items.Count > NumNewItems)
			{
				BuildListView.Items.RemoveAt(BuildListView.Items.Count - 1);
			}

			// Redraw everything
			BuildListView.EndUpdate();
			BuildListView.Invalidate();
		}

		BuildGroup? SelectedBuildGroup;

		void ShowJobContextMenu(Point Point, BuildGroup BuildGroup)
		{
			SelectedBuildGroup = BuildGroup;

			int MinIndex = JobContextMenu.Items.IndexOf(JobContextMenu_StepSeparatorMin) + 1;
			while(JobContextMenu.Items.Count > MinIndex)
			{
				JobContextMenu.Items.RemoveAt(MinIndex);
			}

			JobContextMenu_ViewJob.Text = String.Format("View Job: {0}", BuildGroup.JobName);

			foreach (IssueBuildData Build in BuildGroup.Builds.OrderBy(x => x.JobStepName))
			{
				ToolStripMenuItem MenuItem = new ToolStripMenuItem(String.Format("View Step: {0}", Build.JobStepName));
				MenuItem.Click += (S, E) => Utility.OpenUrl(Build.JobStepUrl);
				JobContextMenu.Items.Insert(MinIndex++, MenuItem);
			}

			JobContextMenu.Show(BuildListView, Point, ToolStripDropDownDirection.BelowRight);
		}

		void UpdateChangeTypeWidget(StatusLineListViewWidget TypeWidget, PerforceChangeDetails? Details)
		{
			TypeWidget.Line.Clear();
			if(Details == null)
			{
				TypeWidget.Line.AddBadge("Unknown", Color.FromArgb(192, 192, 192), null);
			}
			else
			{
				if(Details.bContainsCode)
				{
					TypeWidget.Line.AddBadge("Code", Color.FromArgb(116, 185, 255), null);
				}
				if(Details.bContainsContent)
				{
					TypeWidget.Line.AddBadge("Content", Color.FromArgb(162, 155, 255), null);
				}
			}
		}

		void CreateWorker()
		{
			string? NewSelectedStream = StreamComboBox.SelectedItem as string;
			if(SelectedStream != NewSelectedStream)
			{
				DestroyWorker();
			
				SelectedStream = NewSelectedStream;

				BuildListView.BeginUpdate();
				BuildListView.Items.Clear();

				if(SelectedStream != null)
				{
					SelectedStreamRanges = new List<PerforceChangeRange>();

					int MaxChange = -1;
					foreach(IGrouping<string, IssueBuildData> Group in IssueBuilds.Where(x => x.Stream == SelectedStream).OrderByDescending(x => x.Change).ThenByDescending(x => x.JobUrl).GroupBy(x => x.JobUrl))
					{
						IssueBuildOutcome Outcome = Group.Any(x => x.Outcome == IssueBuildOutcome.Error) ? IssueBuildOutcome.Error : Group.Any(x => x.Outcome == IssueBuildOutcome.Warning) ? IssueBuildOutcome.Warning : IssueBuildOutcome.Success;
						BuildGroup BuildGroup = new BuildGroup(Group.First().JobName, Group.Key, Group.First().Change, Outcome, Group.ToList());

						PerforceChangeRange Range = new PerforceChangeRange(BuildGroup.Change + 1, MaxChange, BuildGroup);
						SelectedStreamRanges.Add(Range);

						MaxChange = BuildGroup.Change;
					}

					PerforceWorker = new PerforceWorkerTask(PerforceSettings, String.Format("{0}/...", SelectedStream), OnRequestComplete, UpdateChangeMetadata, ServiceProvider.GetRequiredService<ILogger<PerforceWorkerTask>>());

					UpdateBuildList();

					for(int Idx = 0; Idx + 2 < SelectedStreamRanges.Count; Idx++)
					{
						if(SelectedStreamRanges[Idx].BuildGroup.Outcome != SelectedStreamRanges[Idx + 1].BuildGroup.Outcome)
						{
							FetchBuildChanges(SelectedStreamRanges[Idx + 1]);
						}
					}
					FetchBuildChanges(SelectedStreamRanges[SelectedStreamRanges.Count - 1]);
				}

				BuildListView.EndUpdate();
				BuildListView.Invalidate();
			}
		}

		void OnUpdateIssues()
		{
			List<IssueData> NewIssues = IssueMonitor.GetIssues();
			foreach(IssueData NewIssue in NewIssues)
			{
				if(NewIssue.Id == Issue.Id)
				{
					Issue = NewIssue;
					UpdateCurrentIssue();
					break;
				}
			}
		}

		private void OnUpdateIssuesAsync()
		{
			MainThreadSynchronizationContext.Post((o) => { if(!bIsDisposing){ OnUpdateIssues(); } }, null);
		}

		void DestroyWorker()
		{
			if(PerforceWorker != null)
			{
				PerforceWorker.Dispose();
				PerforceWorker = null!;
			}
		}

		void OnRequestComplete(PerforceChangeRange Range)
		{
			UpdateBuildList();
		}

		void UpdateChangeMetadata(ChangesRecord Change)
		{
			if(FilterTextBox.Text.Length > 0 || FilterTypeComboBox.SelectedIndex != 0)
			{
				StartUpdateTimer();
			}
			else
			{
				foreach(ListViewItem? Item in BuildListView.Items)
				{
					if(Item != null && Item.Tag == Change)
					{
						PerforceChangeDetailsWithDescribeRecord? Details;
						PerforceWorker!.TryGetChangeDetails(Change.Number, out Details);

						StatusLineListViewWidget TypeWidget = (StatusLineListViewWidget)Item.SubItems[TypeHeader.Index].Tag;
						UpdateChangeTypeWidget(TypeWidget, Details);

						BuildListView.RedrawItems(Item.Index, Item.Index, true);
						break;
					}
				}
			}
		}

		static List<IssueDetailsWindow> ExistingWindows = new List<IssueDetailsWindow>();

		class UpdateIssueDetailsTask
		{
			string ApiUrl;
			long IssueId;
			List<IssueDiagnosticData> Diagnostics;

			public UpdateIssueDetailsTask(string ApiUrl, long IssueId, List<IssueDiagnosticData> Diagnostics)
			{
				this.ApiUrl = ApiUrl;
				this.IssueId = IssueId;
				this.Diagnostics = Diagnostics;
			}

			public async Task RunAsync(CancellationToken CancellationToken)
			{
				Diagnostics.AddRange(await RESTApi.GetAsync<List<IssueDiagnosticData>>($"{ApiUrl}/api/issues/{IssueId}/diagnostics", CancellationToken));
			}
		}

		public static void Show(Form Owner, IssueMonitor IssueMonitor, IPerforceSettings PerforceSettings, TimeSpan? ServerTimeOffset, IssueData Issue, IServiceProvider ServiceProvider, string? CurrentStream)
		{
			Func<CancellationToken, Task<List<IssueBuildData>>> Func = x => RESTApi.GetAsync<List<IssueBuildData>>($"{IssueMonitor.ApiUrl}/api/issues/{Issue.Id}/builds", x);
			ModalTask<List<IssueBuildData>>? IssueBuilds = ModalTask.Execute(Owner, "Querying issue", "Querying issue, please wait...", Func);
			if (IssueBuilds != null && IssueBuilds.Succeeded)
			{
				Show(Owner, IssueMonitor, PerforceSettings, ServerTimeOffset, Issue, IssueBuilds.Result, ServiceProvider, CurrentStream);
			}
		}

		public static void Show(Form Owner, IssueMonitor IssueMonitor, IPerforceSettings PerforceSettings, TimeSpan? ServerTimeOffset, IssueData Issue, List<IssueBuildData> IssueBuilds, IServiceProvider ServiceProvider, string? CurrentStream)
		{
			IssueDetailsWindow Window = ExistingWindows.FirstOrDefault(x => x.IssueMonitor == IssueMonitor && x.Issue.Id == Issue.Id);
			if(Window == null)
			{
				List<IssueDiagnosticData> Diagnostics = new List<IssueDiagnosticData>();

				UpdateIssueDetailsTask Task = new UpdateIssueDetailsTask(IssueMonitor.ApiUrl!, Issue.Id, Diagnostics);
				ModalTask? TaskResult = ModalTask.Execute(Owner, "Fetching data", "Fetching data, please wait...", Task.RunAsync);
				if (TaskResult == null || !TaskResult.Succeeded)
				{
					return;
				}

				Window = new IssueDetailsWindow(IssueMonitor, Issue, IssueBuilds, Diagnostics, PerforceSettings, ServerTimeOffset, ServiceProvider, CurrentStream);
				Window.Owner = Owner;
				if (Owner.Visible && Owner.WindowState != FormWindowState.Minimized)
				{
					Window.StartPosition = FormStartPosition.Manual;
					Window.Location = new Point(Owner.Location.X + (Owner.Width - Window.Width) / 2, Owner.Location.Y + (Owner.Height - Window.Height) / 2);
				}
				else
				{
					Window.StartPosition = FormStartPosition.CenterScreen;
				}
				Window.Show();

				ExistingWindows.Add(Window);
				Window.FormClosed += (S, E) => ExistingWindows.Remove(Window);
			}
			else
			{
				Window.Location = new Point(Owner.Location.X + (Owner.Width - Window.Width) / 2, Owner.Location.Y + (Owner.Height - Window.Height) / 2);
				Window.Activate();
			}
		}

		private void CloseBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.OK;
			Close();
		}

		private void StreamComboBox_SelectedIndexChanged(object sender, EventArgs e)
		{
			DestroyWorker();
			CreateWorker();
		}

		private void BuildListView_DrawColumnHeader(object sender, DrawListViewColumnHeaderEventArgs e)
		{
			e.DrawDefault = true;
		}

		private void BuildListView_DrawItem(object sender, DrawListViewItemEventArgs e)
		{
			if(e.Item.Selected)
			{
				BuildListView.DrawSelectedBackground(e.Graphics, e.Bounds);
			}
			else if(e.ItemIndex == BuildListView.HoverItem)
			{
				BuildListView.DrawTrackedBackground(e.Graphics, e.Bounds);
			}
			else if(e.Item.Tag is BuildGroup)
			{
				BuildGroup BuildGroup = (BuildGroup)e.Item.Tag;

				Color BackgroundColor;
				if(BuildGroup.Outcome == IssueBuildOutcome.Error)
				{
					BackgroundColor = Color.FromArgb(254, 248, 246);
				}
				else if(BuildGroup.Outcome == IssueBuildOutcome.Warning)
				{
					BackgroundColor = Color.FromArgb(254, 254, 246);
				}
				else if(BuildGroup.Outcome == IssueBuildOutcome.Success)
				{
					BackgroundColor = Color.FromArgb(248, 254, 246);
				}
				else
				{
					BackgroundColor = Color.FromArgb(245, 245, 245);
				}

				using(SolidBrush Brush = new SolidBrush(BackgroundColor))
				{
					e.Graphics.FillRectangle(Brush, e.Bounds);
				}
			}
			else
			{
				BuildListView.DrawDefaultBackground(e.Graphics, e.Bounds);
			}
		}

		private void BuildListView_DrawSubItem(object sender, DrawListViewSubItemEventArgs e)
		{
			Font CurrentFont = this.Font;//BuildFont;(Change.Number == Workspace.PendingChangeNumber || Change.Number == Workspace.CurrentChangeNumber)? SelectedBuildFont : BuildFont;

			Color TextColor = SystemColors.WindowText;

			if(e.SubItem.Tag is CustomListViewWidget)
			{
				BuildListView.DrawCustomSubItem(e.Graphics, e.SubItem);
			}
			else if(e.Item.Tag is ChangesRecord)
			{
				ChangesRecord Change = (ChangesRecord)e.Item.Tag;

				Font ChangeFont = BuildListView.Font;
				if(Issue.Owner != null && String.Compare(Change.User, Issue.Owner, StringComparison.OrdinalIgnoreCase) == 0)
				{
					ChangeFont = BoldFont!;
				}

				if(e.ColumnIndex == ChangeHeader.Index)
				{
					TextRenderer.DrawText(e.Graphics, e.SubItem.Text, ChangeFont, e.Bounds, TextColor, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
				}
				else if(e.ColumnIndex == AuthorHeader.Index)
				{
					TextRenderer.DrawText(e.Graphics, e.SubItem.Text, ChangeFont, e.Bounds, TextColor, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
				}
				else if(e.ColumnIndex == DescriptionHeader.Index)
				{
					TextRenderer.DrawText(e.Graphics, e.SubItem.Text, ChangeFont, e.Bounds, TextColor, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
				}
				else if(e.ColumnIndex == TimeHeader.Index)
				{
					TextRenderer.DrawText(e.Graphics, e.SubItem.Text, ChangeFont, e.Bounds, TextColor, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
				}
			}
			else if(e.Item.Tag is IssueBuildData)
			{
				Font BoldFont = BuildListView.Font;

//				TextColor = SystemColors.Window;
				IssueBuildData BuildData = (IssueBuildData)e.Item.Tag;
				if(e.ColumnIndex == IconHeader.Index)
				{
					if(BuildData.Outcome == IssueBuildOutcome.Success)
					{
						BuildListView.DrawIcon(e.Graphics, e.Bounds, WorkspaceControl.GoodBuildIcon);
					}
					else if(BuildData.Outcome == IssueBuildOutcome.Warning)
					{
						BuildListView.DrawIcon(e.Graphics, e.Bounds, WorkspaceControl.MixedBuildIcon);
					}
					else if(BuildData.Outcome == IssueBuildOutcome.Error)
					{
						BuildListView.DrawIcon(e.Graphics, e.Bounds, WorkspaceControl.BadBuildIcon);
					}
					else
					{
						BuildListView.DrawIcon(e.Graphics, e.Bounds, WorkspaceControl.DefaultBuildIcon);
					}
				}
				else if(e.ColumnIndex == ChangeHeader.Index)
				{
					TextRenderer.DrawText(e.Graphics, BuildData.Change.ToString(), BoldFont, e.Bounds, TextColor, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
				}
				else if(e.ColumnIndex == TypeHeader.Index)
				{
					string Status;
					if(BuildData.Outcome == IssueBuildOutcome.Success)
					{
						Status = "Succeeded";
					}
					else if(BuildData.Outcome == IssueBuildOutcome.Warning)
					{
						Status = "Warning";
					}
					else if(BuildData.Outcome == IssueBuildOutcome.Error)
					{
						Status = "Failed";
					}
					else
					{
						Status = "Pending";
					}
					TextRenderer.DrawText(e.Graphics, Status, BoldFont, e.Bounds, TextColor, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
				}
				else if(e.ColumnIndex == AuthorHeader.Index)
				{
					Rectangle Bounds = new Rectangle(e.Bounds.X, e.Bounds.Y, e.Bounds.Width + e.Item.SubItems[e.ColumnIndex].Bounds.Width, e.Bounds.Height);
					TextRenderer.DrawText(e.Graphics, BuildData.JobName, Font, Bounds, TextColor, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
				}
			}
			else
			{
				TextFormatFlags Flags = TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix;
				if(BuildListView.Columns[e.ColumnIndex].TextAlign == HorizontalAlignment.Left)
				{
					Flags |= TextFormatFlags.Left;
				}
				else if(BuildListView.Columns[e.ColumnIndex].TextAlign == HorizontalAlignment.Center)
				{
					Flags |= TextFormatFlags.HorizontalCenter;
				}
				else
				{
					Flags |= TextFormatFlags.Right;
				}
				TextRenderer.DrawText(e.Graphics, e.SubItem.Text, CurrentFont, e.Bounds, TextColor, Flags);
			}
		}

		private void BuildListView_FontChanged(object sender, EventArgs e)
		{
			if(BoldFont != null)
			{
				BoldFont.Dispose();
			}
			BoldFont = new Font(BuildListView.Font.FontFamily, BuildListView.Font.SizeInPoints, FontStyle.Bold);

			if(StatusElementResources != null)
			{
				StatusElementResources.Dispose();
			}
			StatusElementResources = new StatusElementResources(BuildListView.Font);
		}

		private void BuildListView_MouseClick(object Sender, MouseEventArgs Args)
		{
			if(Args.Button == MouseButtons.Right)
			{
				ListViewHitTestInfo HitTest = BuildListView.HitTest(Args.Location);
				if(HitTest.Item != null && HitTest.Item.Tag != null)
				{
					ContextMenuChange = HitTest.Item.Tag as ChangesRecord; 
					if(ContextMenuChange != null)
					{
						BuildListContextMenu_Assign.Text = String.Format("Assign to {0}", ContextMenuChange.User);
						BuildListContextMenu.Show(BuildListView, Args.Location);
					}
				}
			}
		}

		private void BuildListContextMenu_MoreInfo_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				Program.SpawnP4VC(String.Format("{0} change {1}", PerforceSettings.GetArgumentsForExternalProgram(true), ContextMenuChange.Number));
			}
		}

		private void BuildListContextMenu_Blame_Click(object sender, EventArgs e)
		{
			if(ContextMenuChange != null && ContextMenuChange.User != null)
			{
				AssignToUser(ContextMenuChange.User);
			}
		}

		private void AssignToUser(string User)
		{
			IssueUpdateData Update = new IssueUpdateData();
			Update.Id = Issue.Id;
			Update.Owner = User;
			Update.FixChange = 0;
			if(String.Compare(User, PerforceSettings.UserName, StringComparison.OrdinalIgnoreCase) == 0)
			{
				Update.NominatedBy = "";
				Update.Acknowledged = true;
			}
			else
			{
				Update.NominatedBy = PerforceSettings.UserName;
				Update.Acknowledged = false;
			}
			IssueMonitor.PostUpdate(Update);

			UpdateCurrentIssue();
		}

		private void AssignToMeBtn_Click(object sender, EventArgs e)
		{
			AssignToUser(IssueMonitor.UserName);
		}

		private void AssignToOtherBtn_Click(object sender, EventArgs e)
		{
			string? SelectedUserName;
			if(SelectUserWindow.ShowModal(this, PerforceSettings, ServiceProvider, out SelectedUserName))
			{
				AssignToUser(SelectedUserName);
			}
		}

		private void FilterTextBox_TextChanged(object sender, EventArgs e)
		{
			StopUpdateTimer();
			StartUpdateTimer();
		}

		private void FilterTypeComboBox_SelectedIndexChanged(object sender, EventArgs e)
		{
			UpdateBuildList();
		}

		private void MarkFixedBtn_Click(object sender, EventArgs e)
		{
			int FixChangeNumber = Issue.FixChange;
			if(FixChangeNumber == 0)
			{
				if(IssueFixedWindow.ShowModal(this, PerforceSettings, ServiceProvider, ref FixChangeNumber))
				{
					IssueUpdateData Update = new IssueUpdateData();
					Update.Id = Issue.Id;
					Update.FixChange = FixChangeNumber;
					IssueMonitor.PostUpdate(Update);
				}
			}
			else
			{
				IssueUpdateData Update = new IssueUpdateData();
				Update.Id = Issue.Id;
				Update.FixChange = 0;
				IssueMonitor.PostUpdate(Update);
			}
		}

		private void DescriptionLabel_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			IssueBuildData LastBuild = IssueBuilds.Where(x => x.Stream == SelectedStream).OrderByDescending(x => x.Change).ThenByDescending(x => x.ErrorUrl).FirstOrDefault();
			if(LastBuild != null)
			{
				Utility.OpenUrl(LastBuild.ErrorUrl);
			}
		}

		private void JobContextMenu_ShowError_Click(object sender, EventArgs e)
		{
			if (SelectedBuildGroup != null)
			{
				foreach (IssueBuildData Build in SelectedBuildGroup.Builds)
				{
					if (Build.ErrorUrl != null)
					{
						Utility.OpenUrl(Build.ErrorUrl);
						break;
					}
				}
			}
		}

		private void JobContextMenu_ViewJob_Click(object sender, EventArgs e)
		{
			if (SelectedBuildGroup != null)
			{
				Utility.OpenUrl(SelectedBuildGroup.JobUrl);
			}
		}

		private void BuildListView_MouseUp(object sender, MouseEventArgs e)
		{
			if((e.Button & MouseButtons.Right) != 0)
			{
				ListViewHitTestInfo HitTest = BuildListView.HitTest(e.Location);
				if(HitTest.Item != null)
				{
					BuildGroup? Group = HitTest.Item.Tag as BuildGroup;
					if(Group != null)
					{
						ShowJobContextMenu(e.Location, Group);
					}
				}
			}
		}

		private void BuildLinkLabel_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			IssueBuildData Build = IssueBuilds.FirstOrDefault(x => x.ErrorUrl != null);
			if (Build != null)
			{
				Utility.OpenUrl(Build.ErrorUrl);
			}
		}

		private void DetailsTextBox_LinkClicked(object sender, LinkClickedEventArgs e)
		{
			Utility.OpenUrl(e.LinkText);
		}
	}
}
