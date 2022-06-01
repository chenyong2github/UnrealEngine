// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using System.Windows.Forms.VisualStyles;
using System.Threading;
using EpicGames.Core;
using EpicGames.Perforce;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.DependencyInjection;
using System.Diagnostics.CodeAnalysis;

namespace UnrealGameSync
{
	public class UncontrolledChangelist
	{
		public string GUID { get; set; } = String.Empty;
		public string Name { get; set; } = String.Empty;
		public List<string> Files { get; set; } = new List<string>();
	}

	public class UncontrolledChangelistPersistency
	{
		public int Version { get; set; }
		public List<UncontrolledChangelist> Changelists { get; set; } = new List<UncontrolledChangelist>();
	}

	interface IWorkspaceControlOwner
	{
		ToolUpdateMonitor ToolUpdateMonitor { get; }

		void EditSelectedProject(WorkspaceControl Workspace);
		void RequestProjectChange(WorkspaceControl Workspace, UserSelectedProjectSettings Project, bool bModal);
		void ShowAndActivate();
		void StreamChanged(WorkspaceControl Workspace);
		void SetTabNames(TabLabels TabNames);
		void SetupScheduledSync();
		void UpdateProgress();
		void ModifyApplicationSettings();
		void UpdateAlertWindows();
		void UpdateTintColors();

		IssueMonitor CreateIssueMonitor(string? ApiUrl, string UserName);
		void ReleaseIssueMonitor(IssueMonitor IssueMonitor);
	}

	delegate void WorkspaceStartupCallback(WorkspaceControl Workspace, bool bCancel);
	delegate void WorkspaceUpdateCallback(WorkspaceUpdateResult Result);

	partial class WorkspaceControl : UserControl, IMainWindowTabPanel
	{
		enum HorizontalAlignment
		{
			Left,
			Center,
			Right
		}

		enum VerticalAlignment
		{
			Top,
			Middle,
			Bottom
		}

		class BadgeInfo
		{
			public string Label;
			public string? Group;
			public string? UniqueId;
			public int Offset;
			public int Width;
			public int Height;
			public Color BackgroundColor;
			public Color HoverBackgroundColor;
			public Action? ClickHandler;
			public string? ToolTip;

			public BadgeInfo(string Label, string Group, Color BadgeColor)
				: this(Label, Group, null, BadgeColor, BadgeColor, null)
			{
			}

			public BadgeInfo(string Label, string? Group, string? UniqueId, Color BackgroundColor, Color HoverBackgroundColor, Action? ClickHandler)
			{
				this.Label = Label;
				this.Group = Group;
				this.UniqueId = UniqueId;
				this.BackgroundColor = BackgroundColor;
				this.HoverBackgroundColor = HoverBackgroundColor;
				this.ClickHandler = ClickHandler;
			}

			public BadgeInfo(BadgeInfo Other)
				: this(Other.Label, Other.Group, Other.UniqueId, Other.BackgroundColor, Other.HoverBackgroundColor, Other.ClickHandler)
			{
				this.Offset = Other.Offset;
				this.Width = Other.Width;
				this.Height = Other.Height;
			}

			public Rectangle GetBounds(Point ListLocation)
			{
				return new Rectangle(ListLocation.X + Offset, ListLocation.Y, Width, Height);
			}
		}

		class ServiceBadgeInfo : IEquatable<ServiceBadgeInfo>
		{
			public string Name;
			public string? Url;
			public BadgeResult Result;

			public ServiceBadgeInfo(string Name, string? Url, BadgeResult Result)
			{
				this.Name = Name;
				this.Url = Url;
				this.Result = Result;
			}

			public bool Equals(ServiceBadgeInfo? OtherBadge)
			{
				return OtherBadge != null && Name == OtherBadge.Name && Url == OtherBadge.Url && Result == OtherBadge.Result;
			}

			public override bool Equals(object? Other)
			{
				return Equals(Other as ServiceBadgeInfo);
			}

			public override int GetHashCode()
			{
				return Name.GetHashCode() + (Url ?? "").GetHashCode() * 3 + Result.GetHashCode() * 5;
			}
		}

		class ChangeLayoutInfo
		{
			public List<BadgeInfo> DescriptionBadges = new List<BadgeInfo>();
			public List<BadgeInfo> TypeBadges = new List<BadgeInfo>();
			public List<BadgeInfo> BuildBadges = new List<BadgeInfo>();
			public Dictionary<string, List<BadgeInfo>> CustomBadges = new Dictionary<string, List<BadgeInfo>>();
		}

		class BuildListItemComparer : System.Collections.IComparer
		{
			public int Compare(object? A, object? B)
			{
				ChangesRecord? ChangeA = ((ListViewItem?)A)?.Tag as ChangesRecord;
				ChangesRecord? ChangeB = ((ListViewItem?)B)?.Tag as ChangesRecord;
				if (ChangeA == null)
				{
					return +1;
				}
				else if (ChangeB == null)
				{
					return -1;
				}
				else
				{
					return ChangeB.Number - ChangeA.Number;
				}
			}
		}

		public static Rectangle GoodBuildIcon = new Rectangle(0, 0, 16, 16);
		public static Rectangle MixedBuildIcon = new Rectangle(16, 0, 16, 16);
		public static Rectangle BadBuildIcon = new Rectangle(32, 0, 16, 16);
		public static Rectangle DefaultBuildIcon = new Rectangle(48, 0, 16, 16);
		static Rectangle PromotedBuildIcon = new Rectangle(64, 0, 16, 16);
		static Rectangle DetailsIcon = new Rectangle(80, 0, 16, 16);
		static Rectangle InfoIcon = new Rectangle(96, 0, 16, 16);
		static Rectangle CancelIcon = new Rectangle(112, 0, 16, 16);
		static Rectangle SyncIcon = new Rectangle(128, 0, 32, 16);
		static Rectangle HappyIcon = new Rectangle(160, 0, 16, 16);
		static Rectangle DisabledHappyIcon = new Rectangle(176, 0, 16, 16);
		static Rectangle FrownIcon = new Rectangle(192, 0, 16, 16);
		static Rectangle DisabledFrownIcon = new Rectangle(208, 0, 16, 16);
		static Rectangle PreviousSyncIcon = new Rectangle(224, 0, 16, 16);
		static Rectangle AdditionalSyncIcon = new Rectangle(240, 0, 16, 16);
		static Rectangle BisectPassIcon = new Rectangle(256, 0, 16, 16);
		static Rectangle BisectFailIcon = new Rectangle(273, 0, 16, 16);
		static Rectangle BisectImplicitPassIcon = new Rectangle(290, 0, 16, 16);
		static Rectangle BisectImplicitFailIcon = new Rectangle(307, 0, 16, 16);

		[DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
		static extern int SetWindowTheme(IntPtr hWnd, string pszSubAppName, string pszSubIdList);

		const int BuildListExpandCount = 250;

		IWorkspaceControlOwner Owner;
		DirectoryReference AppDataFolder;
		string? ApiUrl;
		DirectoryReference WorkspaceDataFolder;
		IServiceProvider ServiceProvider;
		ILogger Logger;
		IPerforceSettings PerforceSettings;
		ProjectInfo ProjectInfo { get; }

		UserSettings Settings;
		UserWorkspaceState WorkspaceState;
		UserWorkspaceSettings WorkspaceSettings;
		UserProjectSettings ProjectSettings;

		bool bUserHasOpenIssues = false;

		public UserSelectedProjectSettings SelectedProject { get; }

		public FileReference SelectedFileName => ProjectInfo.LocalFileName;
		string SelectedProjectIdentifier => ProjectInfo.ProjectIdentifier;
		public DirectoryReference BranchDirectoryName => ProjectInfo.LocalRootPath;
		public string? StreamName => ProjectInfo.StreamName;
		public string ClientName => ProjectInfo.ClientName;

		SynchronizationContext MainThreadSynchronizationContext;
		bool bIsDisposing;

		JupiterMonitor JupiterMonitor;
		PerforceMonitor PerforceMonitor;
		Workspace Workspace;
		IssueMonitor IssueMonitor;
		EventMonitor EventMonitor;
		System.Windows.Forms.Timer UpdateTimer;
		HashSet<int> PromotedChangeNumbers = new HashSet<int>();
		List<int> ListIndexToChangeIndex = new List<int>();
		List<int> SortedChangeNumbers = new List<int>();
		Dictionary<string, Dictionary<int, string?>> ArchiveToChangeNumberToArchiveKey = new Dictionary<string, Dictionary<int, string?>>();
		Dictionary<int, ChangeLayoutInfo> ChangeNumberToLayoutInfo = new Dictionary<int, ChangeLayoutInfo>();
		List<ContextMenuStrip> CustomStatusPanelMenus = new List<ContextMenuStrip>();
		List<(string, Action<Point, Rectangle>)> CustomStatusPanelLinks = new List<(string, Action<Point, Rectangle>)>();
		int NumChanges;
		int PendingSelectedChangeNumber = -1;
		bool bHasBuildSteps = false;

		Dictionary<string, int> NotifiedBuildTypeToChangeNumber = new Dictionary<string, int>();

		bool bMouseOverExpandLink;

		string? HoverBadgeUniqueId = null;
		bool bHoverSync;
		ChangesRecord? ContextMenuChange;
		Font? BuildFont;
		Font? SelectedBuildFont;
		Font? BadgeFont;
		List<KeyValuePair<string, string>> BadgeNameAndGroupPairs = new List<KeyValuePair<string, string>>();
		Dictionary<string, Size> BadgeLabelToSize = new Dictionary<string, Size>();
		List<ServiceBadgeInfo> ServiceBadges = new List<ServiceBadgeInfo>();

		NotificationWindow NotificationWindow;

		public Tuple<TaskbarState, float> DesiredTaskbarState
		{
			get;
			private set;
		}

		int BuildListWidth;
		float[] ColumnWidths = Array.Empty<float>();
		float[] ColumnWeights = Array.Empty<float>();
		int[] MinColumnWidths = Array.Empty<int>();
		int[] DesiredColumnWidths = Array.Empty<int>();
		string? LastColumnSettings;
		List<ColumnHeader> CustomColumns = new List<ColumnHeader>();
		int MaxBuildBadgeChars;
		ListViewItem? ExpandItem;

		bool bUpdateBuildListPosted;
		bool bUpdateBuildMetadataPosted;
		bool bUpdateReviewsPosted;

		WorkspaceUpdateCallback? UpdateCallback;

		System.Threading.Timer? StartupTimer;
		List<WorkspaceStartupCallback>? StartupCallbacks;

		// When an author filter is applied this will be non-empty and != AuthorFilterPlaceholderText
		string AuthorFilterText = "";
		
		// Placeholder text that is in the control and cleared when the user starts editing.
		static string AuthorFilterPlaceholderText = "<username>";

		public WorkspaceControl(IWorkspaceControlOwner InOwner, DirectoryReference InAppDataFolder, string? InApiUrl, OpenProjectInfo OpenProjectInfo, IServiceProvider InServiceProvider, UserSettings InSettings, OIDCTokenManager? InOidcTokenManager)
		{
			InitializeComponent();

			MainThreadSynchronizationContext = SynchronizationContext.Current!;

			Owner = InOwner;
			AppDataFolder = InAppDataFolder;
			ApiUrl = InApiUrl;
			WorkspaceDataFolder = OpenProjectInfo.ProjectInfo.DataFolder;
			ServiceProvider = InServiceProvider;
			Logger = InServiceProvider.GetRequiredService<ILogger<WorkspaceControl>>();
			PerforceSettings = OpenProjectInfo.PerforceSettings;
			ProjectInfo = OpenProjectInfo.ProjectInfo;
			Settings = InSettings;
			this.WorkspaceSettings = OpenProjectInfo.WorkspaceSettings;
			this.WorkspaceState = OpenProjectInfo.WorkspaceState;
			ProjectSettings = InSettings.FindOrAddProjectSettings(OpenProjectInfo.ProjectInfo, OpenProjectInfo.WorkspaceSettings);

			DesiredTaskbarState = Tuple.Create(TaskbarState.NoProgress, 0.0f);

			System.Reflection.PropertyInfo DoubleBufferedProperty = typeof(Control).GetProperty("DoubleBuffered", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)!;
			DoubleBufferedProperty.SetValue(BuildList, true, null);

			// force the height of the rows
			BuildList.SmallImageList = new ImageList() { ImageSize = new Size(1, 20) };
			BuildList_FontChanged(null, null!);
			BuildList.OnScroll += BuildList_OnScroll;
			BuildList.ListViewItemSorter = new BuildListItemComparer();

			Splitter.OnVisibilityChanged += Splitter_OnVisibilityChanged;

			UpdateTimer = new System.Windows.Forms.Timer();
			UpdateTimer.Interval = 30;
			UpdateTimer.Tick += TimerCallback;

			UpdateCheckedBuildConfig();

			UpdateSyncActionCheckboxes();

			// Set the project logo on the status panel and notification window
			NotificationWindow = new NotificationWindow(Properties.Resources.DefaultNotificationLogo);
			if (ProjectInfo.LocalFileName.HasExtension(".uproject"))
			{
				FileReference LogoFileName = FileReference.Combine(ProjectInfo.LocalFileName.Directory, "Build", "UnrealGameSync.png");
				if (FileReference.Exists(LogoFileName))
				{
					try
					{
						// Duplicate the image, otherwise we'll leave the file locked
						using (Image Image = Image.FromFile(LogoFileName.FullName))
						{
							StatusPanel.SetProjectLogo(new Bitmap(Image), true);
						}
					}
					catch
					{
					}
				}
			}

			// Commit all the new project info
			IPerforceSettings PerforceClientSettings = OpenProjectInfo.PerforceSettings;
			SelectedProject = OpenProjectInfo.SelectedProject;

			// Figure out which API server to use
			string? NewApiUrl;
			if (TryGetProjectSetting(OpenProjectInfo.LatestProjectConfigFile, "ApiUrl", out NewApiUrl))
			{
				ApiUrl = NewApiUrl;
			}

			ProjectInfo Project = OpenProjectInfo.ProjectInfo;

			Workspace = new Workspace(PerforceClientSettings, Project, WorkspaceState, OpenProjectInfo.WorkspaceProjectConfigFile, OpenProjectInfo.WorkspaceProjectStreamFilter, new LogControlTextWriter(SyncLog), ServiceProvider);
			Workspace.OnUpdateComplete += UpdateCompleteCallback;

			FileReference ProjectLogBaseName = FileReference.Combine(WorkspaceDataFolder, "sync.log");

			ILogger PerforceLogger = ServiceProvider.GetRequiredService<ILogger<PerforceMonitor>>();
			PerforceMonitor = new PerforceMonitor(PerforceClientSettings, OpenProjectInfo.ProjectInfo, OpenProjectInfo.LatestProjectConfigFile, OpenProjectInfo.ProjectInfo.CacheFolder, OpenProjectInfo.LocalConfigFiles, ServiceProvider);
			PerforceMonitor.OnUpdate += UpdateBuildListCallback;
			PerforceMonitor.OnUpdateMetadata += UpdateBuildMetadataCallback;
			PerforceMonitor.OnStreamChange += StreamChanged;
			PerforceMonitor.OnLoginExpired += LoginExpired;

			ILogger EventLogger = ServiceProvider.GetRequiredService < ILogger<EventMonitor>>();
			EventMonitor = new EventMonitor(ApiUrl, PerforceUtils.GetClientOrDepotDirectoryName(SelectedProjectIdentifier), OpenProjectInfo.PerforceSettings.UserName, ServiceProvider);
			EventMonitor.OnUpdatesReady += UpdateReviewsCallback;

			ILogger<JupiterMonitor> JupiterLogger = ServiceProvider.GetRequiredService<ILogger<JupiterMonitor>>();
			JupiterMonitor = JupiterMonitor.CreateFromConfigFile(InOidcTokenManager, JupiterLogger, OpenProjectInfo.LatestProjectConfigFile, SelectedProjectIdentifier);

			UpdateColumnSettings(true);

			SyncLog.OpenFile(ProjectLogBaseName);

			Splitter.SetLogVisibility(Settings.bShowLogWindow);

			BuildList.Items.Clear();
			UpdateBuildList();
			UpdateBuildSteps();
			UpdateSyncActionCheckboxes();
			UpdateStatusPanel();
			UpdateServiceBadges();

			PerforceMonitor.Start();
			EventMonitor.Start();

			StartupTimer = new System.Threading.Timer(x => MainThreadSynchronizationContext.Post((o) => { if (!IsDisposed) { StartupTimerElapsed(false); } }, null), null, TimeSpan.FromSeconds(20.0), TimeSpan.FromMilliseconds(-1.0));
			StartupCallbacks = new List<WorkspaceStartupCallback>();

			string? IssuesApiUrl = GetIssuesApiUrl();
			IssueMonitor = InOwner.CreateIssueMonitor(IssuesApiUrl, OpenProjectInfo.PerforceSettings.UserName);
			IssueMonitor.OnIssuesChanged += IssueMonitor_OnIssuesChangedAsync;

			if (SelectedFileName.HasExtension(".uproject"))
			{
				DirectoryReference ConfigDir = DirectoryReference.Combine(SelectedFileName.Directory, "Saved", "Config");

				DirectoryReference.CreateDirectory(ConfigDir);

				EditorConfigWatcher.Path = ConfigDir.FullName;
				EditorConfigWatcher.NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName;
				EditorConfigWatcher.Filter = "*.ini";
				EditorConfigWatcher.IncludeSubdirectories = true;
				EditorConfigWatcher.EnableRaisingEvents = true;

				TintColor = GetTintColor();
			}

			Owner.ToolUpdateMonitor.OnChange += UpdateStatusPanel_CrossThread;
		}

		public void IssueMonitor_OnIssuesChangedAsync()
		{
			MainThreadSynchronizationContext.Post((o) => { if (IssueMonitor != null) { IssueMonitor_OnIssuesChanged(); } }, null);
		}

		public List<IssueData> GetOpenIssuesForUser()
		{
			return IssueMonitor.GetIssues().Where(x => x.FixChange <= 0 && String.Compare(x.Owner, PerforceSettings.UserName, StringComparison.OrdinalIgnoreCase) == 0).ToList();
		}

		public void IssueMonitor_OnIssuesChanged()
		{
			bool bPrevUserHasOpenIssues = bUserHasOpenIssues;
			bUserHasOpenIssues = GetOpenIssuesForUser().Count > 0;
			if (bUserHasOpenIssues != bPrevUserHasOpenIssues)
			{
				UpdateStatusPanel();
				StatusPanel.Invalidate();
			}
		}

		public IssueMonitor GetIssueMonitor()
		{
			return IssueMonitor;
		}

		private void CheckForStartupComplete()
		{
			if (StartupTimer != null)
			{
				int LatestChangeNumber;
				if (FindChangeToSync(Settings.SyncTypeID, out LatestChangeNumber))
				{
					StartupTimerElapsed(false);
				}
			}
		}

		private void StartupTimerElapsed(bool bCancel)
		{
			if (StartupTimer != null)
			{
				StartupTimer.Dispose();
				StartupTimer = null;
			}

			if (StartupCallbacks != null)
			{
				foreach (WorkspaceStartupCallback StartupCallback in StartupCallbacks)
				{
					StartupCallback(this, bCancel);
				}
				StartupCallbacks = null;
			}
		}

		public void AddStartupCallback(WorkspaceStartupCallback StartupCallback)
		{
			if (StartupTimer == null)
			{
				StartupCallback(this, false);
			}
			else
			{
				StartupCallbacks!.Add(StartupCallback);
			}
		}

		private void UpdateColumnSettings(bool bForceUpdate)
		{
			string? NextColumnSettings;
			TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "Columns", out NextColumnSettings);

			if (bForceUpdate || NextColumnSettings != LastColumnSettings)
			{
				LastColumnSettings = NextColumnSettings;

				foreach (ColumnHeader CustomColumn in CustomColumns)
				{
					BuildList.Columns.Remove(CustomColumn);
				}

				Dictionary<string, ColumnHeader> NameToColumn = new Dictionary<string, ColumnHeader>();
				foreach (ColumnHeader? Column in BuildList.Columns)
				{
					if (Column != null)
					{
						NameToColumn[Column.Text] = Column;
						Column.Tag = null;
					}
				}

				CustomColumns = new List<ColumnHeader>();
				if (NextColumnSettings != null)
				{
					foreach (string CustomColumn in NextColumnSettings.Split('\n'))
					{
						ConfigObject ColumnConfig = new ConfigObject(CustomColumn);

						string? Name = ColumnConfig.GetValue("Name", null);
						if (Name != null)
						{
							ColumnHeader? Column;
							if (NameToColumn.TryGetValue(Name, out Column))
							{
								Column.Tag = ColumnConfig;
							}
							else
							{
								int Index = ColumnConfig.GetValue("Index", -1);
								if (Index < 0 || Index > BuildList.Columns.Count)
								{
									Index = ((CustomColumns.Count > 0) ? CustomColumns[CustomColumns.Count - 1].Index : CISColumn.Index) + 1;
								}

								Column = new ColumnHeader();
								Column.Text = Name;
								Column.Tag = ColumnConfig;
								BuildList.Columns.Insert(Index, Column);

								CustomColumns.Add(Column);
							}
						}
					}
				}

				ColumnWidths = new float[BuildList.Columns.Count];
				for (int Idx = 0; Idx < BuildList.Columns.Count; Idx++)
				{
					ColumnWidths[Idx] = BuildList.Columns[Idx].Width;
				}

				using (Graphics Graphics = Graphics.FromHwnd(IntPtr.Zero))
				{
					float DpiScaleX = Graphics.DpiX / 96.0f;

					MinColumnWidths = Enumerable.Repeat(32, BuildList.Columns.Count).ToArray();
					MinColumnWidths[IconColumn.Index] = (int)(50 * DpiScaleX);
					MinColumnWidths[TypeColumn.Index] = (int)(100 * DpiScaleX);
					MinColumnWidths[TimeColumn.Index] = (int)(75 * DpiScaleX);
					MinColumnWidths[ChangeColumn.Index] = (int)(75 * DpiScaleX);
					MinColumnWidths[CISColumn.Index] = (int)(200 * DpiScaleX);

					DesiredColumnWidths = Enumerable.Repeat(65536, BuildList.Columns.Count).ToArray();
					DesiredColumnWidths[IconColumn.Index] = MinColumnWidths[IconColumn.Index];
					DesiredColumnWidths[TypeColumn.Index] = MinColumnWidths[TypeColumn.Index];
					DesiredColumnWidths[TimeColumn.Index] = MinColumnWidths[TimeColumn.Index];
					DesiredColumnWidths[ChangeColumn.Index] = MinColumnWidths[ChangeColumn.Index];
					DesiredColumnWidths[AuthorColumn.Index] = (int)(120 * DpiScaleX);
					DesiredColumnWidths[CISColumn.Index] = (int)(200 * DpiScaleX);
					DesiredColumnWidths[StatusColumn.Index] = (int)(300 * DpiScaleX);

					ColumnWeights = Enumerable.Repeat(1.0f, BuildList.Columns.Count).ToArray();
					ColumnWeights[IconColumn.Index] = 3.0f;
					ColumnWeights[TypeColumn.Index] = 3.0f;
					ColumnWeights[TimeColumn.Index] = 3.0f;
					ColumnWeights[ChangeColumn.Index] = 3.0f;
					ColumnWeights[DescriptionColumn.Index] = 1.25f;
					ColumnWeights[CISColumn.Index] = 1.5f;

					foreach (ColumnHeader? Column in BuildList.Columns)
					{
						if (Column != null)
						{
							ConfigObject? ColumnConfig = (ConfigObject?)Column.Tag;
							if (ColumnConfig != null)
							{
								MinColumnWidths[Column.Index] = (int)(ColumnConfig.GetValue("MinWidth", MinColumnWidths[Column.Index]) * DpiScaleX);
								DesiredColumnWidths[Column.Index] = (int)(ColumnConfig.GetValue("DesiredWidth", DesiredColumnWidths[Column.Index]) * DpiScaleX);
								ColumnWeights[Column.Index] = ColumnConfig.GetValue("Weight", MinColumnWidths[Column.Index]);
							}
						}
					}

					ConfigFile ProjectConfigFile = PerforceMonitor.LatestProjectConfigFile;
					for (int Idx = 0; Idx < BuildList.Columns.Count; Idx++)
					{
						if (!String.IsNullOrEmpty(BuildList.Columns[Idx].Text))
						{
							string? StringValue;
							if (TryGetProjectSetting(ProjectConfigFile, String.Format("ColumnWidth_{0}", BuildList.Columns[Idx].Text), out StringValue))
							{
								int IntValue;
								if (Int32.TryParse(StringValue, out IntValue))
								{
									DesiredColumnWidths[Idx] = (int)(IntValue * DpiScaleX);
								}
							}
						}
					}
				}

				if (ColumnWidths != null)
				{
					ResizeColumns(ColumnWidths.Sum());
				}
			}
		}

		private bool UpdateServiceBadges()
		{
			string[]? ServiceBadgeNames;
			if (!TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "ServiceBadges", out ServiceBadgeNames))
			{
				ServiceBadgeNames = Array.Empty<string>();
			}

			List<ServiceBadgeInfo> PrevServiceBadges = ServiceBadges;

			ServiceBadges = new List<ServiceBadgeInfo>();
			foreach (string ServiceBadgeName in ServiceBadgeNames)
			{
				BadgeData? LatestBuild;
				if (EventMonitor.TryGetLatestBadge(ServiceBadgeName, out LatestBuild))
				{
					ServiceBadges.Add(new ServiceBadgeInfo(ServiceBadgeName, LatestBuild.Url, LatestBuild.Result));
				}
				else
				{
					ServiceBadges.Add(new ServiceBadgeInfo(ServiceBadgeName, null, BadgeResult.Skipped));
				}
			}

			return !Enumerable.SequenceEqual(ServiceBadges, PrevServiceBadges);
		}

		protected override void OnLoad(EventArgs e)
		{
			base.OnLoad(e);

			BuildListWidth = BuildList.Width;

			// Find the default widths 
			ColumnWidths = new float[BuildList.Columns.Count];
			for (int Idx = 0; Idx < BuildList.Columns.Count; Idx++)
			{
				if (DesiredColumnWidths[Idx] > 0)
				{
					ColumnWidths[Idx] = DesiredColumnWidths[Idx];
				}
				else
				{
					ColumnWidths[Idx] = BuildList.Columns[Idx].Width;
				}
			}

			// Resize them to fit the size of the window
			ResizeColumns(BuildList.Width - 32);
		}

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			Owner.ToolUpdateMonitor.OnChange -= UpdateStatusPanel_CrossThread;

			bIsDisposing = true;

			if (disposing && (components != null))
			{
				components.Dispose();
			}

			if (UpdateTimer != null)
			{
				UpdateTimer.Stop();
			}

			if (StartupCallbacks != null)
			{
				foreach (WorkspaceStartupCallback StartupCallback in StartupCallbacks)
				{
					StartupCallback(this, true);
				}
				StartupCallbacks = null!;
			}

			if (IssueMonitor != null)
			{
				IssueMonitor.OnIssuesChanged -= IssueMonitor_OnIssuesChangedAsync;
				Owner.ReleaseIssueMonitor(IssueMonitor);
				IssueMonitor = null!;
			}
			if (StartupTimer != null)
			{
				StartupTimer.Dispose();
				StartupTimer = null!;
			}
			if (NotificationWindow != null)
			{
				NotificationWindow.Dispose();
				NotificationWindow = null!;
			}
			if (PerforceMonitor != null)
			{
				PerforceMonitor.Dispose();
				PerforceMonitor = null!;
			}
			if (Workspace != null)
			{
				Workspace.Dispose();
				Workspace = null!;
			}
			if (EventMonitor != null)
			{
				EventMonitor.Dispose();
				EventMonitor = null!;
			}
			if (BuildFont != null)
			{
				BuildFont.Dispose();
				BuildFont = null!;
			}
			if (SelectedBuildFont != null)
			{
				SelectedBuildFont.Dispose();
				SelectedBuildFont = null!;
			}
			if (BadgeFont != null)
			{
				BadgeFont.Dispose();
				BadgeFont = null!;
			}
			if (JupiterMonitor != null)
			{
				JupiterMonitor.Dispose();
				JupiterMonitor = null!;
			}

			base.Dispose(disposing);
		}

		public bool IsBusy()
		{
			return Workspace.IsBusy();
		}

		public bool CanSyncNow()
		{
			return Workspace != null && !Workspace.IsBusy();
		}

		public bool CanLaunchEditor()
		{
			return Workspace != null && !Workspace.IsBusy() && Workspace.CurrentChangeNumber != -1;
		}

		private void MainWindow_Load(object sender, EventArgs e)
		{
			UpdateStatusPanel();
		}

		private void ShowErrorDialog(string Format, params object[] Args)
		{
			string Message = String.Format(Format, Args);
			Logger.LogError("{Message}", Message);
			MessageBox.Show(Message);
		}

		public bool CanClose()
		{
			CancelWorkspaceUpdate();
			return !Workspace.IsBusy();
		}

		private void StreamChanged()
		{
			Owner.StreamChanged(this);
			/*
			StatusPanel.SuspendDisplay();

			string PrevSelectedFileName = SelectedFileName;
			if(TryCloseProject())
			{
				OpenProject(PrevSelectedFileName);
			}

			StatusPanel.ResumeDisplay();*/
		}

		private void LoginExpired()
		{
			if (!bIsDisposing)
			{
				Logger.LogInformation("Login has expired. Requesting project to be closed.");
				Owner.RequestProjectChange(this, SelectedProject, false);
			}
		}

		private void BuildList_OnScroll()
		{
			PendingSelectedChangeNumber = -1;
		}

		void ShrinkNumRequestedBuilds()
		{
			if (PerforceMonitor != null && BuildList.Items.Count > 0 && PendingSelectedChangeNumber == -1)
			{
				// Find the number of visible items using a (slightly wasteful) binary search
				int VisibleItemCount = 1;
				for (int StepSize = BuildList.Items.Count / 2; StepSize >= 1;)
				{
					int TestIndex = VisibleItemCount + StepSize;
					if (TestIndex < BuildList.Items.Count && BuildList.GetItemRect(TestIndex).Top < BuildList.Height)
					{
						VisibleItemCount += StepSize;
					}
					else
					{
						StepSize /= 2;
					}
				}

				// Figure out the last index to ensure is visible
				int LastVisibleIndex = VisibleItemCount;
				if (LastVisibleIndex >= ListIndexToChangeIndex.Count)
				{
					LastVisibleIndex = ListIndexToChangeIndex.Count - 1;
				}

				// Get the max number of changes to ensure this
				int NewPendingMaxChanges = 0;
				if (LastVisibleIndex >= 0)
				{
					NewPendingMaxChanges = ListIndexToChangeIndex[LastVisibleIndex];
				}
				NewPendingMaxChanges = PerforceMonitor.InitialMaxChangesValue + ((Math.Max(NewPendingMaxChanges - PerforceMonitor.InitialMaxChangesValue, 0) + BuildListExpandCount - 1) / BuildListExpandCount) * BuildListExpandCount;

				// Shrink the number of changes retained by the PerforceMonitor class
				if (PerforceMonitor.PendingMaxChanges > NewPendingMaxChanges)
				{
					PerforceMonitor.PendingMaxChanges = NewPendingMaxChanges;
				}
			}
		}

		void StartSync(int ChangeNumber)
		{
			StartSync(ChangeNumber, false, null);
		}

		void StartSync(int ChangeNumber, bool bSyncOnly, WorkspaceUpdateCallback? Callback)
		{
			WorkspaceUpdateOptions Options = WorkspaceUpdateOptions.Sync | WorkspaceUpdateOptions.SyncArchives | WorkspaceUpdateOptions.GenerateProjectFiles;
			if (!bSyncOnly)
			{
				if (Settings.bBuildAfterSync)
				{
					Options |= WorkspaceUpdateOptions.Build;
				}
				if ((Settings.bBuildAfterSync || ShouldSyncPrecompiledEditor) && Settings.bRunAfterSync)
				{
					Options |= WorkspaceUpdateOptions.RunAfterSync;
				}
				if (Settings.bOpenSolutionAfterSync)
				{
					Options |= WorkspaceUpdateOptions.OpenSolutionAfterSync;
				}
			}
			StartWorkspaceUpdate(ChangeNumber, Options, Callback);
		}

		void StartWorkspaceUpdate(int ChangeNumber, WorkspaceUpdateOptions Options)
		{
			StartWorkspaceUpdate(ChangeNumber, Options, null);
		}

		void StartWorkspaceUpdate(int ChangeNumber, WorkspaceUpdateOptions Options, WorkspaceUpdateCallback? Callback)
		{
			if ((Options & (WorkspaceUpdateOptions.Sync | WorkspaceUpdateOptions.Build)) != 0 && GetProcessesRunningInWorkspace().Length > 0)
			{
				if ((Options & WorkspaceUpdateOptions.ScheduledBuild) != 0)
				{
					SyncLog.Clear();
					SyncLog.AppendLine("Editor is open; scheduled sync has been aborted.");
					return;
				}
				else
				{
					if (!WaitForProgramsToFinish())
					{
						return;
					}
				}
			}

			string[] CombinedSyncFilter = UserSettings.GetCombinedSyncFilter(GetSyncCategories(), Settings.Global.Filter, WorkspaceSettings.Filter);

			ConfigSection PerforceSection = PerforceMonitor.LatestProjectConfigFile.FindSection("Perforce");
			if (PerforceSection != null)
			{
				IEnumerable<string> AdditionalPaths = PerforceSection.GetValues("AdditionalPathsToSync", new string[0]);
				CombinedSyncFilter = AdditionalPaths.Union(CombinedSyncFilter).ToArray();
			}

			WorkspaceUpdateContext Context = new WorkspaceUpdateContext(ChangeNumber, Options, GetEditorBuildConfig(), CombinedSyncFilter, ProjectSettings.BuildSteps, null);
			if (Options.HasFlag(WorkspaceUpdateOptions.SyncArchives))
			{
				IReadOnlyList<IArchiveInfo> Archives = GetArchives();
				foreach (IArchiveInfo Archive in Archives)
				{
					Context.ArchiveTypeToArchive[Archive.Type] = null;
				}

				List<IArchiveInfo> SelectedArchives = GetSelectedArchives(Archives);
				foreach (IArchiveInfo Archive in SelectedArchives)
				{
					string? ArchivePath = GetArchiveKeyForChangeNumber(Archive, ChangeNumber);
					if (ArchivePath == null)
					{
						MessageBox.Show(String.Format("There are no compiled {0} binaries for this change. To sync it, you must disable syncing of precompiled editor binaries.", Archive.Name));
						return;
					}

					if (Archive.Type == IArchiveInfo.EditorArchiveType)
					{
						Context.Options &= ~(WorkspaceUpdateOptions.Build | WorkspaceUpdateOptions.GenerateProjectFiles | WorkspaceUpdateOptions.OpenSolutionAfterSync);
					}

					string[]? ZippedBinariesSyncFilter;
					if (TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "ZippedBinariesSyncFilter", out ZippedBinariesSyncFilter) && ZippedBinariesSyncFilter.Length > 0)
					{
						Context.SyncFilter = Enumerable.Concat(Context.SyncFilter, ZippedBinariesSyncFilter).ToArray();
					}

					Context.ArchiveTypeToArchive[Archive.Type] = new Tuple<IArchiveInfo, string>(Archive, ArchivePath);
				}
			}
			StartWorkspaceUpdate(Context, Callback);
		}

		void StartWorkspaceUpdate(WorkspaceUpdateContext Context, WorkspaceUpdateCallback? Callback)
		{
			if (Settings.bAutoResolveConflicts)
			{
				Context.Options |= WorkspaceUpdateOptions.AutoResolveChanges;
			}
			if (WorkspaceSettings.Filter.AllProjects ?? Settings.Global.Filter.AllProjects ?? false)
			{
				Context.Options |= WorkspaceUpdateOptions.SyncAllProjects | WorkspaceUpdateOptions.IncludeAllProjectsInSolution;
			}
			if (WorkspaceSettings.Filter.AllProjectsInSln ?? Settings.Global.Filter.AllProjectsInSln ?? false)
			{
				Context.Options |= WorkspaceUpdateOptions.IncludeAllProjectsInSolution;
			}

			UpdateCallback = Callback;

			Context.StartTime = DateTime.UtcNow;
			Context.PerforceSyncOptions = (PerforceSyncOptions)Settings.SyncOptions.Clone();

			Logger.LogInformation("Updating workspace at {Time}...", Context.StartTime.ToLocalTime().ToString());
			Logger.LogInformation("  ChangeNumber={Change}", Context.ChangeNumber);
			Logger.LogInformation("  Options={Options}", Context.Options.ToString());
			Logger.LogInformation("  Clobbering {NumFiles} files", Context.ClobberFiles.Count);

			if (Context.Options.HasFlag(WorkspaceUpdateOptions.Sync))
			{
				EventMonitor.PostEvent(Context.ChangeNumber, EventType.Syncing);
			}

			if (Context.Options.HasFlag(WorkspaceUpdateOptions.Sync) || Context.Options.HasFlag(WorkspaceUpdateOptions.Build))
			{
				if (!Context.Options.HasFlag(WorkspaceUpdateOptions.ContentOnly) && (Context.CustomBuildSteps == null || Context.CustomBuildSteps.Count == 0))
				{
					FileReference TargetFile = ConfigUtils.GetEditorTargetFile(Workspace.Project, Workspace.ProjectConfigFile);
					foreach (BuildConfig Config in Enum.GetValues(typeof(BuildConfig)).OfType<BuildConfig>())
					{
						FileReference ReceiptFile = ConfigUtils.GetReceiptFile(Workspace.Project, TargetFile, Config.ToString());
						if (FileReference.Exists(ReceiptFile))
						{
							try { FileReference.Delete(ReceiptFile); } catch (Exception) { }
						}
					}
				}
			}

			SyncLog.Clear();
			Workspace.Update(Context);
			UpdateSyncActionCheckboxes();
			Refresh();
			UpdateTimer.Start();
		}

		void CancelWorkspaceUpdate()
		{
			if (Workspace.IsBusy() && MessageBox.Show("Are you sure you want to cancel the current operation?", "Cancel operation", MessageBoxButtons.YesNo) == DialogResult.Yes)
			{
				WorkspaceState.LastSyncChangeNumber = Workspace.PendingChangeNumber;
				WorkspaceState.LastSyncResult = WorkspaceUpdateResult.Canceled;
				WorkspaceState.LastSyncResultMessage = null;
				WorkspaceState.LastSyncTime = null;
				WorkspaceState.LastSyncDurationSeconds = 0;
				WorkspaceState.Save();

				Workspace.CancelUpdate();

				if (UpdateCallback != null)
				{
					UpdateCallback(WorkspaceUpdateResult.Canceled);
					UpdateCallback = null;
				}

				UpdateTimer.Stop();

				UpdateSyncActionCheckboxes();
				Refresh();
				UpdateStatusPanel();
				DesiredTaskbarState = Tuple.Create(TaskbarState.NoProgress, 0.0f);
				Owner.UpdateProgress();
			}
		}

		void UpdateCompleteCallback(WorkspaceUpdateContext Context, WorkspaceUpdateResult Result, string ResultMessage)
		{
			MainThreadSynchronizationContext.Post((o) => { if (!bIsDisposing) { UpdateComplete(Context, Result, ResultMessage); } }, null);
		}

		void UpdateComplete(WorkspaceUpdateContext Context, WorkspaceUpdateResult Result, string ResultMessage)
		{
			if(bIsDisposing)
			{
				return;
			}

			UpdateTimer.Stop();

			if (Result == WorkspaceUpdateResult.FilesToResolve)
			{
				MessageBox.Show("You have files to resolve after syncing your workspace. Please check P4.");
			}
			else if (Result == WorkspaceUpdateResult.FilesToDelete)
			{
				DesiredTaskbarState = Tuple.Create(TaskbarState.Paused, 0.0f);
				Owner.UpdateProgress();

				DeleteWindow Window = new DeleteWindow(Context.DeleteFiles);
				if (Window.ShowDialog(this) == DialogResult.OK)
				{
					StartWorkspaceUpdate(Context, UpdateCallback);
					return;
				}
			}
			else if (Result == WorkspaceUpdateResult.FilesToClobber)
			{
				DesiredTaskbarState = Tuple.Create(TaskbarState.Paused, 0.0f);
				Owner.UpdateProgress();

				HashSet<string> UncontrolledFiles = new HashSet<string>();
				
				if (SelectedProject.LocalPath != null && SelectedProject.LocalPath.EndsWith(".uprojectdirs", StringComparison.InvariantCultureIgnoreCase))
				{
					List<string> ProjectRoots = GetProjectRoots(SelectedProject.LocalPath);

					foreach (string ProjectRoot in ProjectRoots)
					{
						ParseUncontrolledChangelistsPersistencyFile(ProjectRoot, UncontrolledFiles);
					}
				}
				else
				{
					ParseUncontrolledChangelistsPersistencyFile(Path.GetDirectoryName(SelectedProject.LocalPath)!, UncontrolledFiles);
				}

				ClobberWindow Window = new ClobberWindow(Context.ClobberFiles, UncontrolledFiles);
				
				if (Window.ShowDialog(this) == DialogResult.OK)
				{
					StartWorkspaceUpdate(Context, UpdateCallback);
					return;
				}
			}
			else if (Result == WorkspaceUpdateResult.FailedToCompileWithCleanWorkspace)
			{
				EventMonitor.PostEvent(Context.ChangeNumber, EventType.DoesNotCompile);
			}
			else if (Result == WorkspaceUpdateResult.Success)
			{
				if (Context.Options.HasFlag(WorkspaceUpdateOptions.Build))
				{
					EventMonitor.PostEvent(Context.ChangeNumber, EventType.Compiles);
				}
				if (Context.Options.HasFlag(WorkspaceUpdateOptions.RunAfterSync))
				{
					LaunchEditor();
				}
				if (Context.Options.HasFlag(WorkspaceUpdateOptions.OpenSolutionAfterSync))
				{
					OpenSolution();
				}
			}

			if (UpdateCallback != null)
			{
				UpdateCallback(Result);
				UpdateCallback = null;
			}

			DesiredTaskbarState = Tuple.Create((Result == WorkspaceUpdateResult.Success) ? TaskbarState.NoProgress : TaskbarState.Error, 0.0f);
			Owner.UpdateProgress();

			BuildList.Invalidate();
			Refresh();
			UpdateStatusPanel();
			UpdateSyncActionCheckboxes();

			// Do this last because it may result in the control being disposed
			if (Result == WorkspaceUpdateResult.FailedToSyncLoginExpired)
			{
				LoginExpired();
			}
		}

		void UpdateBuildListCallback()
		{
			if (!bUpdateBuildListPosted)
			{
				bUpdateBuildListPosted = true;
				MainThreadSynchronizationContext.Post((o) => { bUpdateBuildListPosted = false; if (!bIsDisposing) { UpdateBuildList(); } }, null);
			}
		}

		void UpdateBuildList()
		{
			if (SelectedFileName != null)
			{
				ArchiveToChangeNumberToArchiveKey.Clear();
				ChangeNumberToLayoutInfo.Clear();

				List<ChangesRecord> Changes = PerforceMonitor.GetChanges();
				EventMonitor.FilterChanges(Changes.Select(x => x.Number));

				PromotedChangeNumbers = PerforceMonitor.GetPromotedChangeNumbers();

				string[] ExcludeChanges = new string[0];
				if (Workspace != null)
				{
					ConfigFile ProjectConfigFile = PerforceMonitor.LatestProjectConfigFile;
					if (ProjectConfigFile != null)
					{
						ExcludeChanges = ProjectConfigFile.GetValues("Options.ExcludeChanges", ExcludeChanges);
					}
				}

				bool bFirstChange = true;
				bool bHideUnreviewed = !Settings.bShowUnreviewedChanges;

				NumChanges = Changes.Count;
				ListIndexToChangeIndex = new List<int>();
				SortedChangeNumbers = new List<int>();

				List<ChangesRecord> FilteredChanges = new List<ChangesRecord>();
				for (int ChangeIdx = 0; ChangeIdx < Changes.Count; ChangeIdx++)
				{
					ChangesRecord Change = Changes[ChangeIdx];
					if (ShouldShowChange(Change, ExcludeChanges) || PromotedChangeNumbers.Contains(Change.Number))
					{
						SortedChangeNumbers.Add(Change.Number);

						if (!bHideUnreviewed || (!EventMonitor.IsUnderInvestigation(Change.Number) && (ShouldIncludeInReviewedList(Change.Number) || bFirstChange)))
						{
							bFirstChange = false;

							// Add the new change
							FilteredChanges.Add(Change);

							// Store off the list index for this change
							ListIndexToChangeIndex.Add(ChangeIdx);
						}
					}
				}

				UpdateBuildListInternal(FilteredChanges, Changes.Count > 0);

				SortedChangeNumbers.Sort();

				if (PendingSelectedChangeNumber != -1)
				{
					SelectChange(PendingSelectedChangeNumber);
				}
			}

			if (BuildList.HoverItem > BuildList.Items.Count)
			{
				BuildList.HoverItem = -1;
			}

			UpdateBuildFailureNotification();

			UpdateBuildSteps();
			UpdateSyncActionCheckboxes();
		}

		void UpdateBuildListInternal(List<ChangesRecord> Changes, bool bShowExpandItem)
		{
			BuildList.BeginUpdate();

			// Remove any changes that no longer exist, and update the rest
			Dictionary<int, ChangesRecord> ChangeNumberToSummary = Changes.ToDictionary(x => x.Number, x => x);
			for (int Idx = BuildList.Items.Count - 1; Idx >= 0; Idx--)
			{
				ChangesRecord? Change = BuildList.Items[Idx].Tag as ChangesRecord;
				if (Change != null)
				{
					ChangesRecord? Summary;
					if (ChangeNumberToSummary.TryGetValue(Change.Number, out Summary))
					{
						// Update
						BuildList.Items[Idx].SubItems[DescriptionColumn.Index].Text = Change.Description!.Replace('\n', ' ');
						ChangeNumberToSummary.Remove(Change.Number);
					}
					else
					{
						// Delete
						BuildList.Items.RemoveAt(Idx);
					}
				}
			}

			// Add everything left over
			foreach (ChangesRecord Change in ChangeNumberToSummary.Values)
			{
				BuildList_AddItem(Change);
			}

			// Figure out which group the expand item should be in
			ListViewGroup? NewExpandItemGroup = null;
			for (int Idx = BuildList.Items.Count - 1; Idx >= 0; Idx--)
			{
				ListViewItem Item = BuildList.Items[Idx];
				if (Item != ExpandItem)
				{
					NewExpandItemGroup = Item.Group;
					break;
				}
			}

			// Remove the expand row if it's in the wrong place
			if (ExpandItem != null)
			{
				if (!bShowExpandItem || ExpandItem.Group != NewExpandItemGroup)
				{
					BuildList.Items.Remove(ExpandItem);
					ExpandItem = null;
				}
			}

			// Remove any empty groups
			for (int Idx = BuildList.Groups.Count - 1; Idx >= 0; Idx--)
			{
				if (BuildList.Groups[Idx].Items.Count == 0)
				{
					BuildList.Groups.RemoveAt(Idx);
				}
			}

			// Add the expand row back in
			if (bShowExpandItem && ExpandItem == null)
			{
				ExpandItem = new ListViewItem(NewExpandItemGroup);
				ExpandItem.Tag = null;
				ExpandItem.Selected = false;
				ExpandItem.Text = "";
				for (int ColumnIdx = 1; ColumnIdx < BuildList.Columns.Count; ColumnIdx++)
				{
					ExpandItem.SubItems.Add(new ListViewItem.ListViewSubItem(ExpandItem, ""));
				}
				BuildList.Items.Add(ExpandItem);
			}

			BuildList.EndUpdate();
		}

		ListViewGroup BuildList_FindOrAddGroup(ChangesRecord Change, string GroupName)
		{
			// Find or add the new group
			int GroupIndex = 0;
			for (; GroupIndex < BuildList.Groups.Count; GroupIndex++)
			{
				ListViewGroup NextGroup = BuildList.Groups[GroupIndex];
				if (NextGroup.Header == GroupName)
				{
					return NextGroup;
				}

				ChangesRecord? LastChange = null;
				for (int Idx = NextGroup.Items.Count - 1; Idx >= 0 && LastChange == null; Idx--)
				{
					LastChange = NextGroup.Items[Idx].Tag as ChangesRecord;
				}
				if (LastChange == null || Change.Number > LastChange.Number)
				{
					break;
				}
			}

			// Create the new group
			ListViewGroup Group = new ListViewGroup(GroupName);
			BuildList.Groups.Insert(GroupIndex, Group);
			return Group;
		}

		void BuildList_AddItem(ChangesRecord Change)
		{
			// Get the display time for this item
			DateTime DisplayTime;
			if (Settings.bShowLocalTimes)
			{
				DisplayTime = Change.Time.ToLocalTime();
			}
			else
			{
				DisplayTime = new DateTime(Change.Time.Ticks + PerforceMonitor.ServerTimeZone.Ticks, DateTimeKind.Local);
			}

			// Find or add the new group
			ListViewGroup Group = BuildList_FindOrAddGroup(Change, DisplayTime.ToString("D"));

			// Create the new item
			ListViewItem Item = new ListViewItem();
			Item.Tag = Change;

			// Get the new text for each column
			string[] Columns = new string[BuildList.Columns.Count];
			Columns[ChangeColumn.Index] = String.Format("{0}", Change.Number);

			Columns[TimeColumn.Index] = DisplayTime.ToShortTimeString();
			string UserName = FormatUserName(Change.User!);

			// If annotate robomerge is on, add a prefix to the user name
			if (Settings.bAnnotateRobmergeChanges && IsRobomergeChange(Change))
			{
				UserName += " (robo)";
			}

			Columns[AuthorColumn.Index] = UserName;
			Columns[DescriptionColumn.Index] = Change.Description!.Replace('\n', ' ');

			for (int ColumnIdx = 1; ColumnIdx < BuildList.Columns.Count; ColumnIdx++)
			{
				Item.SubItems.Add(new ListViewItem.ListViewSubItem(Item, Columns[ColumnIdx] ?? ""));
			}

			// Insert it at the right position within the group
			int GroupInsertIdx = 0;
			for (; GroupInsertIdx < Group.Items.Count; GroupInsertIdx++)
			{
				ChangesRecord? OtherChange = Group.Items[GroupInsertIdx].Tag as ChangesRecord;
				if (OtherChange == null || Change.Number >= OtherChange.Number)
				{
					break;
				}
			}
			Group.Items.Insert(GroupInsertIdx, Item);

			// Insert it into the build list
			BuildList.Items.Add(Item);
		}

		/// <summary>
		/// Returns true if this change was submitted on behalf of someone by robomerge
		/// </summary>
		/// <param name="Change"></param>
		/// <returns></returns>
		bool IsRobomergeChange(ChangesRecord Change)
		{
			// If hiding robomerge, filter out based on workspace name. Note - don't look at the description because we
			// *do* want to see conflicts that were manually merged by someone
			return Change.Client!.IndexOf("ROBOMERGE", StringComparison.OrdinalIgnoreCase) >= 0;
		}

		bool ShouldShowChange(ChangesRecord Change, string[] ExcludeChanges)
		{
			if (ProjectSettings.FilterType != FilterType.None)
			{
				PerforceChangeDetails? Details;
				if (!PerforceMonitor.TryGetChangeDetails(Change.Number, out Details))
				{
					return false;
				}
				if (ProjectSettings.FilterType == FilterType.Code && !Details.bContainsCode)
				{
					return false;
				}
				if (ProjectSettings.FilterType == FilterType.Content && !Details.bContainsContent)
				{
					return false;
				}
			}

			// if filtering by user, only show changes where the author contains the filter text
			if (!string.IsNullOrEmpty(this.AuthorFilterText)
				&& this.AuthorFilterText != AuthorFilterPlaceholderText
				&& Change.User!.IndexOf(this.AuthorFilterText, StringComparison.OrdinalIgnoreCase) < 0)
			{
				return false;
			}
			
			// If this is a robomerge change, check if any filters will cause it to be hidden
			if (IsRobomergeChange(Change))
			{
				if (Settings.ShowRobomerge == UserSettings.RobomergeShowChangesOption.None)
				{
					return false;
				}
				else if (Settings.ShowRobomerge == UserSettings.RobomergeShowChangesOption.Badged)
				{
					// if this change has any badges, we'll show it unless it's later filtered out
					EventSummary? Summary = EventMonitor.GetSummaryForChange(Change.Number);
					if (Summary == null || !Summary.Badges.Any())
					{
						return false;
					}
				}
			}

			if (ProjectSettings.FilterBadges.Count > 0)
			{
				EventSummary? Summary = EventMonitor.GetSummaryForChange(Change.Number);
				if (Summary == null || !Summary.Badges.Any(x => ProjectSettings.FilterBadges.Contains(x.BadgeName)))
				{
					return false;
				}
			}
			if (!Settings.bShowAutomatedChanges)
			{
				foreach (string ExcludeChange in ExcludeChanges)
				{
					if (Regex.IsMatch(Change.Description, ExcludeChange, RegexOptions.IgnoreCase))
					{
						return false;
					}
				}

				if (String.Compare(Change.User, "buildmachine", true) == 0 && Change.Description!.IndexOf("lightmaps", StringComparison.InvariantCultureIgnoreCase) == -1)
				{
					return false;
				}
			}
			if (IsBisectModeEnabled() && !WorkspaceState.BisectChanges.Any(x => x.Change == Change.Number))
			{
				return false;
			}
			return true;
		}

		void UpdateBuildMetadataCallback()
		{
			if (!bUpdateBuildMetadataPosted)
			{
				bUpdateBuildMetadataPosted = true;
				MainThreadSynchronizationContext.Post((o) => { bUpdateBuildMetadataPosted = false; if (!bIsDisposing) { UpdateBuildMetadata(); } }, null);
			}
		}

		string? GetIssuesApiUrl()
		{
			string? IssuesApiUrl;
			if (!TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "IssuesApiUrl", out IssuesApiUrl))
			{
				IssuesApiUrl = ApiUrl;
			}
			return IssuesApiUrl;
		}

		void UpdateBuildMetadata()
		{
			// Refresh the issue monitor if it's changed
			string? IssuesApiUrl = GetIssuesApiUrl();
			if (IssuesApiUrl != IssueMonitor.ApiUrl)
			{
				Logger.LogInformation("Changing issues API url from {OldApiUrl} to {ApiUrl}", IssueMonitor.ApiUrl, IssuesApiUrl);
				IssueMonitor NewIssueMonitor = Owner.CreateIssueMonitor(IssuesApiUrl, PerforceSettings.UserName);
				Owner.ReleaseIssueMonitor(IssueMonitor);
				IssueMonitor = NewIssueMonitor;
			}

			// Update the column settings first, since this may affect the badges we hide
			UpdateColumnSettings(false);

			// Clear the badge size cache
			BadgeLabelToSize.Clear();

			// Reset the badge groups
			Dictionary<string, string> BadgeNameToGroup = new Dictionary<string, string>();

			// Read the group mappings from the config file
			string? GroupDefinitions;
			if (TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "BadgeGroups", out GroupDefinitions))
			{
				string[] GroupDefinitionsArray = GroupDefinitions.Split('\n');
				for (int Idx = 0; Idx < GroupDefinitionsArray.Length; Idx++)
				{
					string GroupName = String.Format("{0:0000}", Idx);
					foreach (string BadgeName in GroupDefinitionsArray[Idx].Split(',').Select(x => x.Trim()))
					{
						BadgeNameToGroup[BadgeName] = GroupName;
					}
				}
			}

			// Add a dummy group for any other badges we have
			foreach (ListViewItem? Item in BuildList.Items)
			{
				if (Item != null && Item.Tag != null)
				{
					EventSummary? Summary = EventMonitor.GetSummaryForChange(((ChangesRecord)Item.Tag).Number);
					if (Summary != null)
					{
						foreach (BadgeData Badge in Summary.Badges)
						{
							string BadgeSlot = Badge.BadgeName;
							if (!BadgeNameToGroup.ContainsKey(BadgeSlot))
							{
								BadgeNameToGroup.Add(BadgeSlot, "XXXX");
							}
						}
					}
				}
			}

			// Remove anything that's a service badge
			foreach (ServiceBadgeInfo ServiceBadge in ServiceBadges)
			{
				BadgeNameToGroup.Remove(ServiceBadge.Name);
			}

			// Remove everything that's in a custom column
			foreach (ColumnHeader CustomColumn in CustomColumns)
			{
				ConfigObject ColumnConfig = (ConfigObject)CustomColumn.Tag;
				foreach (string BadgeName in ColumnConfig.GetValue("Badges", "").Split(','))
				{
					BadgeNameToGroup.Remove(BadgeName);
				}
			}

			// Sort the list of groups for display
			BadgeNameAndGroupPairs = BadgeNameToGroup.OrderBy(x => x.Value).ThenBy(x => x.Key).ToList();

			// Figure out whether to show smaller badges due to limited space
			UpdateMaxBuildBadgeChars();

			// Update everything else
			ArchiveToChangeNumberToArchiveKey.Clear();
			ChangeNumberToLayoutInfo.Clear();
			BuildList.Invalidate();
			UpdateServiceBadges();
			UpdateStatusPanel();
			UpdateBuildFailureNotification();
			CheckForStartupComplete();

			// If we are filtering by badges, we may also need to update the build list
			if (ProjectSettings.FilterType != FilterType.None || ProjectSettings.FilterBadges.Count > 0)
			{
				UpdateBuildList();
			}
		}

		void UpdateMaxBuildBadgeChars()
		{
			int NewMaxBuildBadgeChars;
			if (GetBuildBadgeStripWidth(-1) < CISColumn.Width)
			{
				NewMaxBuildBadgeChars = -1;
			}
			else if (GetBuildBadgeStripWidth(3) < CISColumn.Width)
			{
				NewMaxBuildBadgeChars = 3;
			}
			else if (GetBuildBadgeStripWidth(2) < CISColumn.Width)
			{
				NewMaxBuildBadgeChars = 2;
			}
			else
			{
				NewMaxBuildBadgeChars = 1;
			}

			if (NewMaxBuildBadgeChars != MaxBuildBadgeChars)
			{
				ChangeNumberToLayoutInfo.Clear();
				BuildList.Invalidate();
				MaxBuildBadgeChars = NewMaxBuildBadgeChars;
			}
		}

		int GetBuildBadgeStripWidth(int MaxNumChars)
		{
			// Create dummy badges for each badge name
			List<BadgeInfo> DummyBadges = new List<BadgeInfo>();
			foreach (KeyValuePair<string, string> BadgeNameAndGroupPair in BadgeNameAndGroupPairs)
			{
				string BadgeName = BadgeNameAndGroupPair.Key;
				if (MaxNumChars != -1 && BadgeName.Length > MaxNumChars)
				{
					BadgeName = BadgeName.Substring(0, MaxNumChars);
				}

				BadgeInfo DummyBadge = CreateBadge(-1, BadgeName, BadgeNameAndGroupPair.Value, null);
				DummyBadges.Add(DummyBadge);
			}

			// Check if they fit within the column
			int Width = 0;
			if (DummyBadges.Count > 0)
			{
				LayoutBadges(DummyBadges);
				Width = DummyBadges[DummyBadges.Count - 1].Offset + DummyBadges[DummyBadges.Count - 1].Width;
			}
			return Width;
		}

		bool ShouldIncludeInReviewedList(int ChangeNumber)
		{
			if (PromotedChangeNumbers.Contains(ChangeNumber))
			{
				return true;
			}

			EventSummary? Review = EventMonitor.GetSummaryForChange(ChangeNumber);
			if (Review != null)
			{
				if (Review.LastStarReview != null && Review.LastStarReview.Type == EventType.Starred)
				{
					return true;
				}
				if (Review.Verdict == ReviewVerdict.Good || Review.Verdict == ReviewVerdict.Mixed)
				{
					return true;
				}
			}
			return false;
		}

		Dictionary<Guid, WorkspaceSyncCategory> GetSyncCategories()
		{
			return ConfigUtils.GetSyncCategories(PerforceMonitor.LatestProjectConfigFile);
		}

		List<string> GetProjectRoots(string InUProjectDirsPath)
		{
			List<string> ProjectRoots = new List<string>();

			if (!File.Exists(InUProjectDirsPath))
			{
				return ProjectRoots;
			}

			string UProjectDirsRoot = Path.GetDirectoryName(InUProjectDirsPath)!;

			foreach (string Line in File.ReadLines(InUProjectDirsPath))
			{
				// Remove unnecessary whitespaces
				string TrimmedLine = Line.Trim();

				// Remove every comments
				int CommentIndex = TrimmedLine.IndexOf(';');

				if (CommentIndex >= 0)
				{
					TrimmedLine = TrimmedLine.Substring(0, CommentIndex);
				}

				if (TrimmedLine.Length <= 0)
				{
					continue;
				}

				string UProjectDirsEntry = Path.Combine(UProjectDirsRoot, TrimmedLine);

				if (!Directory.Exists(UProjectDirsEntry))
				{
					continue;
				}

				List<string> EntryDirectories = new List<string>(Directory.EnumerateDirectories(UProjectDirsEntry));

				ProjectRoots = ProjectRoots.Concat(EntryDirectories).ToList();
			}

			return ProjectRoots;
		}

		void ParseUncontrolledChangelistsPersistencyFile(string InProjectRoot, HashSet<string> OutUncontrolledFiles)
		{
			String UncontrolledChangelistPersistencyFilePath = Path.Combine(InProjectRoot, "Saved", "SourceControl", "UncontrolledChangelists.json");

			if (File.Exists(UncontrolledChangelistPersistencyFilePath))
			{
				try
				{
					string JsonString = File.ReadAllText(UncontrolledChangelistPersistencyFilePath);
					UncontrolledChangelistPersistency UCLPersistency = JsonSerializer.Deserialize<UncontrolledChangelistPersistency>(JsonString, Utility.DefaultJsonSerializerOptions);

					foreach (UncontrolledChangelist UCL in UCLPersistency.Changelists)
					{
						foreach (string UncontrolledFile in UCL.Files)
						{
							OutUncontrolledFiles.Add(UncontrolledFile);
						}
					}
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Exception while parsing Uncontrolled Changelist persistency file");
				}
			}
		}

		void UpdateReviewsCallback()
		{
			if (!bUpdateReviewsPosted)
			{
				bUpdateReviewsPosted = true;
				MainThreadSynchronizationContext.Post((o) => { bUpdateReviewsPosted = false; if (!bIsDisposing) { UpdateReviews(); } }, null);
			}
		}

		void UpdateReviews()
		{
			ArchiveToChangeNumberToArchiveKey.Clear();
			ChangeNumberToLayoutInfo.Clear();
			EventMonitor.ApplyUpdates();

			if (UpdateServiceBadges())
			{
				UpdateStatusPanel();
			}

			Refresh();
			UpdateBuildFailureNotification();
			CheckForStartupComplete();
		}

		void UpdateBuildFailureNotification()
		{
			if (!DeploymentSettings.EnableAlerts)
			{
				return;
			}

			// Ignore this if we're using the new build health system
			if (GetDefaultIssueFilter() != null)
			{
				return;
			}

			int LastChangeByCurrentUser = PerforceMonitor.LastChangeByCurrentUser;
			int LastCodeChangeByCurrentUser = PerforceMonitor.LastCodeChangeByCurrentUser;

			// Find all the badges which should notify users due to content changes
			HashSet<string> ContentBadges = new HashSet<string>();
			ContentBadges.UnionWith(PerforceMonitor.LatestProjectConfigFile.GetValues("Notifications.ContentBadges", new string[0]));

			// Find the most recent build of each type, and the last time it succeeded
			Dictionary<string, BadgeData> TypeToLastBuild = new Dictionary<string, BadgeData>();
			Dictionary<string, BadgeData> TypeToLastSucceededBuild = new Dictionary<string, BadgeData>();
			for (int Idx = SortedChangeNumbers.Count - 1; Idx >= 0; Idx--)
			{
				EventSummary? Summary = EventMonitor.GetSummaryForChange(SortedChangeNumbers[Idx]);
				if (Summary != null)
				{
					foreach (BadgeData Badge in Summary.Badges)
					{
						if (!TypeToLastBuild.ContainsKey(Badge.BuildType) && (Badge.Result == BadgeResult.Success || Badge.Result == BadgeResult.Warning || Badge.Result == BadgeResult.Failure))
						{
							TypeToLastBuild.Add(Badge.BuildType, Badge);
						}
						if (!TypeToLastSucceededBuild.ContainsKey(Badge.BuildType) && Badge.Result == BadgeResult.Success)
						{
							TypeToLastSucceededBuild.Add(Badge.BuildType, Badge);
						}
					}
				}
			}

			// Find all the build types that the user needs to be notified about.
			int RequireNotificationForChange = -1;
			List<BadgeData> NotifyBuilds = new List<BadgeData>();
			foreach (BadgeData LastBuild in TypeToLastBuild.Values.OrderBy(x => x.BuildType))
			{
				if (LastBuild.Result == BadgeResult.Failure || LastBuild.Result == BadgeResult.Warning)
				{
					// Get the last submitted changelist by this user of the correct type
					int LastChangeByCurrentUserOfType;
					if (ContentBadges.Contains(LastBuild.BuildType))
					{
						LastChangeByCurrentUserOfType = LastChangeByCurrentUser;
					}
					else
					{
						LastChangeByCurrentUserOfType = LastCodeChangeByCurrentUser;
					}

					// Check if the failed build was after we submitted
					if (LastChangeByCurrentUserOfType > 0 && LastBuild.ChangeNumber >= LastChangeByCurrentUserOfType)
					{
						// And check that there wasn't a successful build after we submitted (if there was, we're in the clear)
						BadgeData? LastSuccessfulBuild;
						if (!TypeToLastSucceededBuild.TryGetValue(LastBuild.BuildType, out LastSuccessfulBuild) || LastSuccessfulBuild.ChangeNumber < LastChangeByCurrentUserOfType)
						{
							// Add it to the list of notifications
							NotifyBuilds.Add(LastBuild);

							// Check if this is a new notification, rather than one we've already dismissed
							int NotifiedChangeNumber;
							if (!NotifiedBuildTypeToChangeNumber.TryGetValue(LastBuild.BuildType, out NotifiedChangeNumber) || NotifiedChangeNumber < LastChangeByCurrentUserOfType)
							{
								RequireNotificationForChange = Math.Max(RequireNotificationForChange, LastChangeByCurrentUserOfType);
							}
						}
					}
				}
			}

			// If there's anything we haven't already notified the user about, do so now
			if (RequireNotificationForChange != -1)
			{
				// Format the platform list
				StringBuilder PlatformList = new StringBuilder();
				PlatformList.AppendFormat("{0}: {1}", StreamName, NotifyBuilds[0].BuildType);
				for (int Idx = 1; Idx < NotifyBuilds.Count - 1; Idx++)
				{
					PlatformList.AppendFormat(", {0}", NotifyBuilds[Idx].BuildType);
				}
				if (NotifyBuilds.Count > 1)
				{
					PlatformList.AppendFormat(" and {0}", NotifyBuilds[NotifyBuilds.Count - 1].BuildType);
				}

				// Show the balloon tooltip
				if (NotifyBuilds.Any(x => x.Result == BadgeResult.Failure))
				{
					string Title = String.Format("{0} Errors", PlatformList.ToString());
					string Message = String.Format("CIS failed after your last submitted changelist ({0}).", RequireNotificationForChange);
					NotificationWindow.Show(NotificationType.Error, Title, Message);
				}
				else
				{
					string Title = String.Format("{0} Warnings", PlatformList.ToString());
					string Message = String.Format("CIS completed with warnings after your last submitted changelist ({0}).", RequireNotificationForChange);
					NotificationWindow.Show(NotificationType.Warning, Title, Message);
				}

				// Set the link to open the right build pages
				int HighlightChange = NotifyBuilds.Max(x => x.ChangeNumber);
				NotificationWindow.OnMoreInformation = () => { Owner.ShowAndActivate(); SelectChange(HighlightChange); };

				// Don't show messages for this change again
				foreach (BadgeData NotifyBuild in NotifyBuilds)
				{
					NotifiedBuildTypeToChangeNumber[NotifyBuild.BuildType] = RequireNotificationForChange;
				}
			}
		}

		private void BuildList_DrawColumnHeader(object sender, DrawListViewColumnHeaderEventArgs e)
		{
			e.DrawDefault = true;
		}

		class ExpandRowLayout
		{
			public string? MainText;
			public Rectangle MainRect;
			public string? LinkText;
			public Rectangle LinkRect;
		}

		ExpandRowLayout LayoutExpandRow(Graphics Graphics, Rectangle Bounds)
		{
			ExpandRowLayout Layout = new ExpandRowLayout();

			string ShowingChanges = String.Format("Showing {0}/{1} changes.", ListIndexToChangeIndex.Count, NumChanges);

			int CurrentMaxChanges = PerforceMonitor.CurrentMaxChanges;
			if (PerforceMonitor.PendingMaxChanges > CurrentMaxChanges)
			{
				Layout.MainText = String.Format("{0}. Fetching {1} more from server...  ", ShowingChanges, PerforceMonitor.PendingMaxChanges - CurrentMaxChanges);
				Layout.LinkText = "Cancel";
			}
			else if (PerforceMonitor.CurrentMaxChanges > NumChanges)
			{
				Layout.MainText = ShowingChanges;
				Layout.LinkText = "";
			}
			else
			{
				Layout.MainText = String.Format("{0}  ", ShowingChanges);
				Layout.LinkText = "Show more...";
			}

			Size MainTextSize = TextRenderer.MeasureText(Graphics, Layout.MainText, BuildFont, new Size(1000, 1000), TextFormatFlags.NoPadding);
			Size LinkTextSize = TextRenderer.MeasureText(Graphics, Layout.LinkText, BuildFont, new Size(1000, 1000), TextFormatFlags.NoPadding);

			int MinX = Bounds.Left + (Bounds.Width - MainTextSize.Width - LinkTextSize.Width) / 2;
			int MinY = Bounds.Bottom - MainTextSize.Height - 1;

			Layout.MainRect = new Rectangle(new Point(MinX, MinY), MainTextSize);
			Layout.LinkRect = new Rectangle(new Point(MinX + MainTextSize.Width, MinY), LinkTextSize);

			return Layout;
		}

		private void DrawExpandRow(Graphics Graphics, ExpandRowLayout Layout)
		{
			TextRenderer.DrawText(Graphics, Layout.MainText, BuildFont, Layout.MainRect, SystemColors.WindowText, TextFormatFlags.SingleLine | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix | TextFormatFlags.NoPadding);

			Color LinkColor = SystemColors.HotTrack;
			if (bMouseOverExpandLink)
			{
				LinkColor = Color.FromArgb(LinkColor.B, LinkColor.G, LinkColor.R);
			}

			TextRenderer.DrawText(Graphics, Layout.LinkText, BuildFont, Layout.LinkRect, LinkColor, TextFormatFlags.SingleLine | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix | TextFormatFlags.NoPadding);
		}

		private bool HitTestExpandLink(Point Location)
		{
			if (ExpandItem == null)
			{
				return false;
			}
			using (Graphics Graphics = Graphics.FromHwnd(IntPtr.Zero))
			{
				ExpandRowLayout Layout = LayoutExpandRow(Graphics, ExpandItem.Bounds);
				return Layout.LinkRect.Contains(Location);
			}
		}

		private void BuildList_DrawItem(object sender, DrawListViewItemEventArgs e)
		{
			if (e.Item == ExpandItem)
			{
				BuildList.DrawDefaultBackground(e.Graphics, e.Bounds);

				ExpandRowLayout ExpandLayout = LayoutExpandRow(e.Graphics, e.Bounds);
				DrawExpandRow(e.Graphics, ExpandLayout);
			}
			else if (Workspace != null)
			{
				if (e.Item.Selected)
				{
					BuildList.DrawSelectedBackground(e.Graphics, e.Bounds);
				}
				else if (e.ItemIndex == BuildList.HoverItem)
				{
					BuildList.DrawTrackedBackground(e.Graphics, e.Bounds);
				}
				else if (((ChangesRecord)e.Item.Tag).Number == Workspace.PendingChangeNumber)
				{
					BuildList.DrawTrackedBackground(e.Graphics, e.Bounds);
				}
				else
				{
					BuildList.DrawDefaultBackground(e.Graphics, e.Bounds);
				}
			}
		}

		private string? GetArchiveKeyForChangeNumber(IArchiveInfo Archive, int ChangeNumber)
		{
			string? ArchivePath;

			Dictionary<int, string?>? ChangeNumberToArchivePath;
			if (!ArchiveToChangeNumberToArchiveKey.TryGetValue(Archive.Name, out ChangeNumberToArchivePath))
			{
				ChangeNumberToArchivePath = new Dictionary<int, string?>();
				ArchiveToChangeNumberToArchiveKey[Archive.Name] = ChangeNumberToArchivePath;
			}

			if (!ChangeNumberToArchivePath.TryGetValue(ChangeNumber, out ArchivePath))
			{
				PerforceChangeDetails? Details;
				if (PerforceMonitor.TryGetChangeDetails(ChangeNumber, out Details))
				{
					// Try to get the archive for this CL
					if (!Archive.TryGetArchiveKeyForChangeNumber(ChangeNumber, out ArchivePath) && !Details.bContainsCode)
					{
						// Otherwise if it's a content-only change, find the previous build any use the archive path from that
						int Index = SortedChangeNumbers.BinarySearch(ChangeNumber);
						if (Index > 0)
						{
							ArchivePath = GetArchiveKeyForChangeNumber(Archive, SortedChangeNumbers[Index - 1]);
						}
					}
				}
				ChangeNumberToArchivePath.Add(ChangeNumber, ArchivePath);
			}

			return ArchivePath;
		}

		private Color Blend(Color First, Color Second, float T)
		{
			return Color.FromArgb((int)(First.R + (Second.R - First.R) * T), (int)(First.G + (Second.G - First.G) * T), (int)(First.B + (Second.B - First.B) * T));
		}

		private bool CanSyncChange(int ChangeNumber)
		{
			if (PerforceMonitor == null)
			{
				return false;
			}

			List<IArchiveInfo> SelectedArchives = GetSelectedArchives(GetArchives());
			return SelectedArchives.Count == 0 || SelectedArchives.All(x => GetArchiveKeyForChangeNumber(x, ChangeNumber) != null);
		}

		/// <summary>
		/// Should read as Do Required Badges Exist and Are They Good.
		/// Determines if the ini specified INI filter is a subset of the InBadges passed
		/// BadgeGroupSyncFilter - is used as the ini 
		/// </summary>
		/// <param name="InBadges"></param>
		/// <returns></returns>
		private bool DoRequiredBadgesExist(List<string> RequiredBadgeList, List<BadgeData> InBadges)
		{
			Dictionary<string, BadgeData> InBadgeDictionary = new Dictionary<string, BadgeData>();
			foreach (BadgeData Badge in InBadges)
			{
				InBadgeDictionary.Add(Badge.BadgeName, Badge);
			}

			// make sure all required badges exist in the InBadgeSet
			foreach (string BadgeName in RequiredBadgeList)
			{
				BadgeData? Badge;
				if (InBadgeDictionary.TryGetValue(BadgeName, out Badge))
				{
					// If any required badge is not successful then the filter isn't matched.
					if (!Badge.IsSuccess)
					{
						return false;
					}
				}
				else
				{
					// required badge not found.
					return false;
				}
			}

			// All required badges existed and were successful 
			return true;
		}

		private bool CanSyncChangeType(LatestChangeType ChangeType, ChangesRecord Change, EventSummary? Summary)
		{
			if (ChangeType.bGood)
			{
				if (Summary == null || Summary.Verdict != ReviewVerdict.Good)
				{
					return false;
				}
			}

			if (ChangeType.bStarred)
			{
				if ((Summary == null || Summary.LastStarReview == null || Summary.LastStarReview.Type != EventType.Starred) && !PromotedChangeNumbers.Contains(Change.Number))
				{
					return false;
				}
			}

			if (ChangeType.RequiredBadges.Count > 0)
			{
				if (Summary == null || !DoRequiredBadgesExist(ChangeType.RequiredBadges, Summary.Badges))
				{
					return false;
				}
			}

			return true;
		}

		private ChangeLayoutInfo GetChangeLayoutInfo(ChangesRecord Change)
		{
			ChangeLayoutInfo? LayoutInfo;
			if (!ChangeNumberToLayoutInfo.TryGetValue(Change.Number, out LayoutInfo))
			{
				LayoutInfo = new ChangeLayoutInfo();

				LayoutInfo.DescriptionBadges = CreateDescriptionBadges(Change);
				LayoutInfo.TypeBadges = CreateTypeBadges(Change.Number);

				EventSummary? Summary = EventMonitor.GetSummaryForChange(Change.Number);
				LayoutInfo.BuildBadges = CreateBuildBadges(Change.Number, Summary);
				LayoutInfo.CustomBadges = CreateCustomBadges(Change.Number, Summary);

				ChangeNumberToLayoutInfo[Change.Number] = LayoutInfo;
			}
			return LayoutInfo;
		}

		private void GetRemainingBisectRange(out int OutPassChangeNumber, out int OutFailChangeNumber)
		{
			int PassChangeNumber = -1;
			foreach (BisectEntry Entry in WorkspaceState.BisectChanges)
			{
				if (Entry.State == BisectState.Pass && (Entry.Change > PassChangeNumber || PassChangeNumber == -1))
				{
					PassChangeNumber = Entry.Change;
				}
			}

			int FailChangeNumber = -1;
			foreach (BisectEntry Entry in WorkspaceState.BisectChanges)
			{
				if (Entry.State == BisectState.Fail && Entry.Change > PassChangeNumber && (Entry.Change < FailChangeNumber || FailChangeNumber == -1))
				{
					FailChangeNumber = Entry.Change;
				}
			}

			OutPassChangeNumber = PassChangeNumber;
			OutFailChangeNumber = FailChangeNumber;
		}

		private void BuildList_DrawSubItem(object sender, DrawListViewSubItemEventArgs e)
		{
			ChangesRecord Change = (ChangesRecord)e.Item.Tag;
			if (Change == null)
			{
				return;
			}

			float DpiScaleX = e.Graphics.DpiX / 96.0f;
			float DpiScaleY = e.Graphics.DpiY / 96.0f;

			float IconY = e.Bounds.Top + (e.Bounds.Height - 16 * DpiScaleY) / 2;

			StringFormat Format = new StringFormat();
			Format.LineAlignment = StringAlignment.Center;
			Format.FormatFlags = StringFormatFlags.NoWrap;
			Format.Trimming = StringTrimming.EllipsisCharacter;

			Font CurrentFont = (Change.Number == Workspace.PendingChangeNumber || Change.Number == Workspace.CurrentChangeNumber) ? SelectedBuildFont! : BuildFont!;

			bool bAllowSync = CanSyncChange(Change.Number);
			int BadgeAlpha = bAllowSync ? 255 : 128;
			Color TextColor = (bAllowSync || Change.Number == Workspace.PendingChangeNumber || Change.Number == Workspace.CurrentChangeNumber || WorkspaceState.AdditionalChangeNumbers.Contains(Change.Number)) ? SystemColors.WindowText : Blend(SystemColors.Window, SystemColors.WindowText, 0.25f);

			const int FadeRange = 6;
			if (e.ItemIndex >= BuildList.Items.Count - FadeRange && NumChanges >= PerforceMonitor.CurrentMaxChanges && !IsBisectModeEnabled())
			{
				float Opacity = (float)(BuildList.Items.Count - e.ItemIndex - 0.9f) / FadeRange;
				BadgeAlpha = (int)(BadgeAlpha * Opacity);
				TextColor = Blend(SystemColors.Window, TextColor, Opacity);
			}

			if (e.ColumnIndex == IconColumn.Index)
			{
				EventSummary? Summary = EventMonitor.GetSummaryForChange(Change.Number);

				float MinX = 4 * DpiScaleX;
				if ((Summary != null && EventMonitor.WasSyncedByCurrentUser(Summary.ChangeNumber)) || (Workspace != null && Workspace.CurrentChangeNumber == Change.Number))
				{
					e.Graphics.DrawImage(Properties.Resources.Icons, MinX * DpiScaleX, IconY, PreviousSyncIcon, GraphicsUnit.Pixel);
				}
				else if (WorkspaceSettings != null && WorkspaceState.AdditionalChangeNumbers.Contains(Change.Number))
				{
					e.Graphics.DrawImage(Properties.Resources.Icons, MinX * DpiScaleX, IconY, AdditionalSyncIcon, GraphicsUnit.Pixel);
				}
				else if (bAllowSync && ((Summary != null && Summary.LastStarReview != null && Summary.LastStarReview.Type == EventType.Starred) || PromotedChangeNumbers.Contains(Change.Number)))
				{
					e.Graphics.DrawImage(Properties.Resources.Icons, MinX * DpiScaleX, IconY, PromotedBuildIcon, GraphicsUnit.Pixel);
				}
				MinX += PromotedBuildIcon.Width * DpiScaleX;

				if (bAllowSync)
				{
					Rectangle QualityIcon = DefaultBuildIcon;

					if (IsBisectModeEnabled())
					{
						int FirstPass, FirstFail;
						GetRemainingBisectRange(out FirstPass, out FirstFail);

						BisectEntry Entry = WorkspaceState.BisectChanges.FirstOrDefault(x => x.Change == Change.Number);
						if (Entry == null || Entry.State == BisectState.Exclude)
						{
							QualityIcon = new Rectangle(0, 0, 0, 0);
						}
						else if (Entry.State == BisectState.Pass)
						{
							QualityIcon = BisectPassIcon;
						}
						else if (Entry.State == BisectState.Fail)
						{
							QualityIcon = BisectFailIcon;
						}
						else if (FirstFail != -1 && Change.Number > FirstFail)
						{
							QualityIcon = BisectImplicitFailIcon;
						}
						else if (FirstPass != -1 && Change.Number < FirstPass)
						{
							QualityIcon = BisectImplicitPassIcon;
						}
					}
					else
					{
						if (EventMonitor.IsUnderInvestigation(Change.Number))
						{
							QualityIcon = BadBuildIcon;
						}
						else if (Summary != null)
						{
							if (Summary.Verdict == ReviewVerdict.Good)
							{
								QualityIcon = GoodBuildIcon;
							}
							else if (Summary.Verdict == ReviewVerdict.Bad)
							{
								QualityIcon = BadBuildIcon;
							}
							else if (Summary.Verdict == ReviewVerdict.Mixed)
							{
								QualityIcon = MixedBuildIcon;
							}
						}
					}
					e.Graphics.DrawImage(Properties.Resources.Icons, MinX, IconY, QualityIcon, GraphicsUnit.Pixel);

					MinX += QualityIcon.Width * DpiScaleX;
				}
			}
			else if (e.ColumnIndex == ChangeColumn.Index || e.ColumnIndex == TimeColumn.Index)
			{
				TextRenderer.DrawText(e.Graphics, e.SubItem.Text, CurrentFont, e.Bounds, TextColor, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
			}
			else if (e.ColumnIndex == AuthorColumn.Index)
			{
				TextRenderer.DrawText(e.Graphics, e.SubItem.Text, CurrentFont, e.Bounds, TextColor, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
			}
			else if (e.ColumnIndex == DescriptionColumn.Index)
			{
				ChangeLayoutInfo Layout = GetChangeLayoutInfo(Change);

				Rectangle RemainingBounds = e.Bounds;
				if (Layout.DescriptionBadges.Count > 0)
				{
					e.Graphics.IntersectClip(e.Bounds);
					e.Graphics.SmoothingMode = SmoothingMode.HighQuality;

					RemainingBounds = new Rectangle(RemainingBounds.Left, RemainingBounds.Top, RemainingBounds.Width - (int)(2 * DpiScaleX), RemainingBounds.Height);

					Point ListLocation = GetBadgeListLocation(Layout.DescriptionBadges, RemainingBounds, HorizontalAlign.Right, VerticalAlignment.Middle);
					DrawBadges(e.Graphics, ListLocation, Layout.DescriptionBadges, BadgeAlpha);

					RemainingBounds = new Rectangle(RemainingBounds.Left, RemainingBounds.Top, ListLocation.X - RemainingBounds.Left - (int)(2 * DpiScaleX), RemainingBounds.Height);
				}

				TextRenderer.DrawText(e.Graphics, e.SubItem.Text, CurrentFont, RemainingBounds, TextColor, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
			}
			else if (e.ColumnIndex == TypeColumn.Index)
			{
				ChangeLayoutInfo Layout = GetChangeLayoutInfo(Change);
				if (Layout.TypeBadges.Count > 0)
				{
					e.Graphics.IntersectClip(e.Bounds);
					e.Graphics.SmoothingMode = SmoothingMode.HighQuality;
					Point TypeLocation = GetBadgeListLocation(Layout.TypeBadges, e.Bounds, HorizontalAlign.Center, VerticalAlignment.Middle);
					DrawBadges(e.Graphics, TypeLocation, Layout.TypeBadges, BadgeAlpha);
				}
			}
			else if (e.ColumnIndex == CISColumn.Index)
			{
				ChangeLayoutInfo Layout = GetChangeLayoutInfo(Change);
				if (Layout.BuildBadges.Count > 0)
				{
					e.Graphics.IntersectClip(e.Bounds);
					e.Graphics.SmoothingMode = SmoothingMode.HighQuality;

					Point BuildsLocation = GetBadgeListLocation(Layout.BuildBadges, e.Bounds, HorizontalAlign.Center, VerticalAlignment.Middle);
					BuildsLocation.X = Math.Max(BuildsLocation.X, e.Bounds.Left);

					DrawBadges(e.Graphics, BuildsLocation, Layout.BuildBadges, BadgeAlpha);
				}
			}
			else if (e.ColumnIndex == StatusColumn.Index)
			{
				float MaxX = e.SubItem.Bounds.Right;

				if (Change.Number == Workspace.PendingChangeNumber && Workspace.IsBusy())
				{
					Tuple<string, float> Progress = Workspace.CurrentProgress;

					MaxX -= CancelIcon.Width;
					e.Graphics.DrawImage(Properties.Resources.Icons, MaxX, IconY, CancelIcon, GraphicsUnit.Pixel);

					if (!Splitter.IsLogVisible())
					{
						MaxX -= InfoIcon.Width;
						e.Graphics.DrawImage(Properties.Resources.Icons, MaxX, IconY, InfoIcon, GraphicsUnit.Pixel);
					}

					float MinX = e.Bounds.Left;

					TextRenderer.DrawText(e.Graphics, Progress.Item1, CurrentFont, new Rectangle((int)MinX, e.Bounds.Top, (int)(MaxX - MinX), e.Bounds.Height), TextColor, TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
				}
				else
				{
					e.Graphics.IntersectClip(e.Bounds);
					e.Graphics.SmoothingMode = SmoothingMode.HighQuality;

					if (Change.Number == Workspace.CurrentChangeNumber)
					{
						EventData? Review = EventMonitor.GetReviewByCurrentUser(Change.Number);

						MaxX -= FrownIcon.Width * DpiScaleX;
						e.Graphics.DrawImage(Properties.Resources.Icons, MaxX, IconY, (Review == null || !EventMonitor.IsPositiveReview(Review.Type)) ? FrownIcon : DisabledFrownIcon, GraphicsUnit.Pixel);

						MaxX -= HappyIcon.Width * DpiScaleX;
						e.Graphics.DrawImage(Properties.Resources.Icons, MaxX, IconY, (Review == null || !EventMonitor.IsNegativeReview(Review.Type)) ? HappyIcon : DisabledHappyIcon, GraphicsUnit.Pixel);
					}
					else if (e.ItemIndex == BuildList.HoverItem && bAllowSync)
					{
						Rectangle SyncBadgeRectangle = GetSyncBadgeRectangle(e.SubItem.Bounds);
						DrawBadge(e.Graphics, SyncBadgeRectangle, "Sync", bHoverSync ? Color.FromArgb(140, 180, 230) : Color.FromArgb(112, 146, 190), true, true);
						MaxX = SyncBadgeRectangle.Left - (int)(2 * DpiScaleX);
					}

					string? SummaryText;
					if (WorkspaceState.LastSyncChangeNumber == -1 || WorkspaceState.LastSyncChangeNumber != Change.Number || !GetLastUpdateMessage(WorkspaceState.LastSyncResult, WorkspaceState.LastSyncResultMessage, out SummaryText))
					{
						StringBuilder SummaryTextBuilder = new StringBuilder();

						EventSummary? Summary = EventMonitor.GetSummaryForChange(Change.Number);

						AppendItemList(SummaryTextBuilder, " ", "Under investigation by {0}.", EventMonitor.GetInvestigatingUsers(Change.Number).Select(x => FormatUserName(x)));

						if (Summary != null)
						{
							string Comments = String.Join(", ", Summary.Comments.Where(x => !String.IsNullOrWhiteSpace(x.Text)).Select(x => String.Format("{0}: \"{1}\"", FormatUserName(x.UserName), x.Text)));
							if (Comments.Length > 0)
							{
								SummaryTextBuilder.Append(((SummaryTextBuilder.Length == 0) ? "" : " ") + Comments);
							}
							else
							{
								AppendItemList(SummaryTextBuilder, " ", "Used by {0}.", Summary.CurrentUsers.Select(x => FormatUserName(x)));
							}
						}

						SummaryText = (SummaryTextBuilder.Length == 0) ? "No current users." : SummaryTextBuilder.ToString();
					}

					if (SummaryText != null && SummaryText.Contains('\n'))
					{
						SummaryText = SummaryText.Substring(0, SummaryText.IndexOf('\n')).TrimEnd() + "...";
					}

					TextRenderer.DrawText(e.Graphics, SummaryText, CurrentFont, new Rectangle(e.Bounds.Left, e.Bounds.Top, (int)MaxX - e.Bounds.Left, e.Bounds.Height), TextColor, TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
				}
			}
			else
			{
				ColumnHeader Column = BuildList.Columns[e.ColumnIndex];
				if (CustomColumns.Contains(Column))
				{
					ChangeLayoutInfo Layout = GetChangeLayoutInfo(Change);

					List<BadgeInfo>? Badges;
					if (Layout.CustomBadges.TryGetValue(Column.Text, out Badges) && Badges.Count > 0)
					{
						e.Graphics.IntersectClip(e.Bounds);
						e.Graphics.SmoothingMode = SmoothingMode.HighQuality;

						Point BuildsLocation = GetBadgeListLocation(Badges, e.Bounds, HorizontalAlign.Center, VerticalAlignment.Middle);
						DrawBadges(e.Graphics, BuildsLocation, Badges, BadgeAlpha);
					}
				}
			}
		}

		private void LayoutBadges(List<BadgeInfo> Badges)
		{
			int Offset = 0;
			for (int Idx = 0; Idx < Badges.Count; Idx++)
			{
				BadgeInfo Badge = Badges[Idx];

				if (Idx > 0 && Badge.Group != Badges[Idx - 1].Group)
				{
					Offset += 6;
				}
				Badge.Offset = Offset;

				Size BadgeSize = GetBadgeSize(Badge.Label);
				Badge.Width = BadgeSize.Width;
				Badge.Height = BadgeSize.Height;

				Offset += BadgeSize.Width + 1;
			}
		}

		private Point GetBadgeListLocation(List<BadgeInfo> Badges, Rectangle Bounds, HorizontalAlign HorzAlign, VerticalAlignment VertAlign)
		{
			Point Location = Bounds.Location;
			if (Badges.Count == 0)
			{
				return Bounds.Location;
			}

			BadgeInfo LastBadge = Badges[Badges.Count - 1];

			int X = Bounds.X;
			switch (HorzAlign)
			{
				case HorizontalAlign.Center:
					X += (Bounds.Width - LastBadge.Width - LastBadge.Offset) / 2;
					break;
				case HorizontalAlign.Right:
					X += (Bounds.Width - LastBadge.Width - LastBadge.Offset);
					break;
			}

			int Y = Bounds.Y;
			switch (VertAlign)
			{
				case VerticalAlignment.Middle:
					Y += (Bounds.Height - LastBadge.Height) / 2;
					break;
				case VerticalAlignment.Bottom:
					Y += (Bounds.Height - LastBadge.Height);
					break;
			}

			return new Point(X, Y);
		}

		private void DrawBadges(Graphics Graphics, Point ListLocation, List<BadgeInfo> Badges, int Alpha)
		{
			for (int Idx = 0; Idx < Badges.Count; Idx++)
			{
				BadgeInfo Badge = Badges[Idx];
				bool bMergeLeft = (Idx > 0 && Badges[Idx - 1].Group == Badge.Group);
				bool bMergeRight = (Idx < Badges.Count - 1 && Badges[Idx + 1].Group == Badge.Group);

				Rectangle Bounds = new Rectangle(ListLocation.X + Badge.Offset, ListLocation.Y, Badge.Width, Badge.Height);
				if (Badge.UniqueId != null && Badge.UniqueId == HoverBadgeUniqueId)
				{
					DrawBadge(Graphics, Bounds, Badge.Label, Color.FromArgb((Alpha * Badge.HoverBackgroundColor.A) / 255, Badge.HoverBackgroundColor), bMergeLeft, bMergeRight);
				}
				else
				{
					DrawBadge(Graphics, Bounds, Badge.Label, Color.FromArgb((Alpha * Badge.BackgroundColor.A) / 255, Badge.BackgroundColor), bMergeLeft, bMergeRight);
				}
			}
		}

		private List<BadgeInfo> CreateDescriptionBadges(ChangesRecord Change)
		{
			string Description = Change.Description ?? String.Empty;

			PerforceChangeDetails? Details;
			if (PerforceMonitor.TryGetChangeDetails(Change.Number, out Details))
			{
				Description = Details.Description;
			}

			List<BadgeInfo> Badges = new List<BadgeInfo>();

			try
			{
				ConfigFile ProjectConfigFile = PerforceMonitor.LatestProjectConfigFile;
				if (ProjectConfigFile != null)
				{
					string[] BadgeDefinitions = ProjectConfigFile.GetValues("Badges.DescriptionBadges", new string[0]);
					foreach (string BadgeDefinition in BadgeDefinitions.Distinct())
					{
						ConfigObject BadgeDefinitionObject = new ConfigObject(BadgeDefinition);
						string? Pattern = BadgeDefinitionObject.GetValue("Pattern", null);
						string? Name = BadgeDefinitionObject.GetValue("Name", null);
						string? Group = BadgeDefinitionObject.GetValue("Group", null);
						string Color = BadgeDefinitionObject.GetValue("Color", "#909090");
						string HoverColor = BadgeDefinitionObject.GetValue("HoverColor", "#b0b0b0");
						string? Url = BadgeDefinitionObject.GetValue("Url", null);
						string? Arguments = BadgeDefinitionObject.GetValue("Arguments", null);
						if (!String.IsNullOrEmpty(Name) && !String.IsNullOrEmpty(Pattern))
						{
							foreach (Match? MatchResult in Regex.Matches(Description, Pattern, RegexOptions.Multiline))
							{
								if (MatchResult != null)
								{
									Color BadgeColor = System.Drawing.ColorTranslator.FromHtml(Color);
									Color HoverBadgeColor = System.Drawing.ColorTranslator.FromHtml(HoverColor);

									string? UniqueId = String.IsNullOrEmpty(Url) ? null : String.Format("Description:{0}:{1}", Change.Number, Badges.Count);

									string? ExpandedUrl = ReplaceRegexMatches(Url, MatchResult);
									string? ExpandedArguments = ReplaceRegexMatches(Arguments, MatchResult);

									Action? ClickHandler;
									if (String.IsNullOrEmpty(ExpandedUrl))
									{
										ClickHandler = null;
									}
									else if (String.IsNullOrEmpty(ExpandedArguments))
									{
										ClickHandler = () => SafeProcessStart(ExpandedUrl);
									}
									else
									{
										ClickHandler = () => SafeProcessStart(ExpandedUrl, ExpandedArguments);
									}

									Badges.Add(new BadgeInfo(ReplaceRegexMatches(Name, MatchResult), Group, UniqueId, BadgeColor, HoverBadgeColor, ClickHandler));
								}
							}
						}
					}
				}
			}
			catch (Exception)
			{
			}

			LayoutBadges(Badges);

			return Badges;
		}

		private void PrintLatestChangeTypeUsage(string ErroringDefinition)
		{
			string ErrorMessage = "Error: Name not set for config value in \"Sync.LatestChangeType\"";
			string UsageMessage = "Expected Format under the [Sync] ini category: +LatestChangeType=(Name=[string], Description=[string], OrderIndex=[int], bGood=[bool], bStarred=[bool], RequiredBadges=\"List of Badges For Changelist to have\")";

			ShowErrorDialog("{0} Erroring String({1}) \n {2}", ErrorMessage, ErroringDefinition, UsageMessage);
		}


		/**
		 * Array of config specified options for what the Latest Change to Sync should be.
		 * See PrintLatestChangeTypeUsage for usage information
		 */
		public List<LatestChangeType> GetCustomLatestChangeTypes()
		{
			List<LatestChangeType> FoundLatestChangeTypes = new List<LatestChangeType>();

			// Add three default change types that can be synced to
			FoundLatestChangeTypes.Add(LatestChangeType.LatestChange());
			FoundLatestChangeTypes.Add(LatestChangeType.LatestGoodChange());
			FoundLatestChangeTypes.Add(LatestChangeType.LatestStarredChange());

			string[] LatestChangeTypeDefinitions = PerforceMonitor.LatestProjectConfigFile.GetValues("Sync.LatestChangeType", new string[0]);
			foreach (string LatestChangeDefinition in LatestChangeTypeDefinitions.Distinct())
			{
				LatestChangeType? NewType;
				if (LatestChangeType.TryParseConfigEntry(LatestChangeDefinition, out NewType))
				{
					FoundLatestChangeTypes.Add(NewType);
				}
				else
				{
					PrintLatestChangeTypeUsage(LatestChangeDefinition);
				}
			}

			FoundLatestChangeTypes = FoundLatestChangeTypes.OrderBy(X => X.OrderIndex).ToList();

			return FoundLatestChangeTypes;
		}

		private static void SafeProcessStart(ProcessStartInfo StartInfo)
		{
			try
			{
				Process.Start(StartInfo);
			}
			catch
			{
				MessageBox.Show(String.Format("Unable to open {0}", StartInfo.FileName));
			}
		}

		private static void SafeProcessStart(string Url)
		{
			try
			{
				ProcessStartInfo StartInfo = new ProcessStartInfo();
				StartInfo.FileName = Url;
				StartInfo.UseShellExecute = true;
				using Process _ = Process.Start(StartInfo);
			}
			catch
			{
				MessageBox.Show(String.Format("Unable to open {0}", Url));
			}
		}

		private static void SafeProcessStart(string Url, string Arguments)
		{
			try
			{
				ProcessStartInfo StartInfo = new ProcessStartInfo();
				StartInfo.FileName = Url;
				StartInfo.Arguments = Arguments;
				StartInfo.UseShellExecute = true;
				using Process _ = Process.Start(StartInfo);
			}
			catch
			{
				MessageBox.Show(String.Format("Unable to open {0} {1}", Url, Arguments));
			}
		}

		private static void SafeProcessStart(string Url, string Arguments, string WorkingDir)
		{
			try
			{
				ProcessStartInfo StartInfo = new ProcessStartInfo();
				StartInfo.FileName = Url;
				StartInfo.Arguments = Arguments;
				StartInfo.WorkingDirectory = WorkingDir;
				StartInfo.UseShellExecute = true;
				using Process _ = Process.Start(StartInfo);
			}
			catch
			{
				MessageBox.Show(String.Format("Unable to open {0} {1}", Url, Arguments));
			}
		}

		public void UpdateSettings()
		{
			UpdateStatusPanel();
		}

		[return: NotNullIfNotNull("Text")]
		private string? ReplaceRegexMatches(string? Text, Match MatchResult)
		{
			if (Text != null)
			{
				for (int Idx = 1; Idx < MatchResult.Groups.Count; Idx++)
				{
					string SourceText = String.Format("${0}", Idx);
					Text = Text.Replace(SourceText, MatchResult.Groups[Idx].Value);
				}
			}
			return Text;
		}

		private List<BadgeInfo> CreateTypeBadges(int ChangeNumber)
		{
			List<BadgeInfo> Badges = new List<BadgeInfo>();

			PerforceChangeDetails? Details;
			if (PerforceMonitor.TryGetChangeDetails(ChangeNumber, out Details))
			{
				if (Details.bContainsCode)
				{
					Badges.Add(new BadgeInfo("Code", "ChangeType", Color.FromArgb(116, 185, 255)));
				}
				if (Details.bContainsContent)
				{
					Badges.Add(new BadgeInfo("Content", "ChangeType", Color.FromArgb(162, 155, 255)));
				}
			}
			if (Badges.Count == 0)
			{
				Badges.Add(new BadgeInfo("Unknown", "ChangeType", Color.FromArgb(192, 192, 192)));
			}
			LayoutBadges(Badges);

			return Badges;
		}

		private bool TryGetProjectSetting(ConfigFile ProjectConfigFile, string Name, [NotNullWhen(true)] out string? Value)
		{
			return ConfigUtils.TryGetProjectSetting(ProjectConfigFile, SelectedProjectIdentifier, Name, out Value);
		}

		private bool TryGetProjectSetting(ConfigFile ProjectConfigFile, string Name, [NotNullWhen(true)] out string[]? Values)
		{
			string? ValueList;
			if (TryGetProjectSetting(ProjectConfigFile, Name, out ValueList))
			{
				Values = ValueList.Split('\n').Select(x => x.Trim()).Where(x => x.Length > 0).ToArray();
				return true;
			}
			else
			{
				Values = null;
				return false;
			}
		}

		private bool TryGetProjectSetting(ConfigFile ProjectConfigFile, string Name, string LegacyName, [NotNullWhen(true)] out string? Value)
		{
			string? NewValue;
			if (TryGetProjectSetting(ProjectConfigFile, Name, out NewValue))
			{
				Value = NewValue;
				return true;
			}

			NewValue = ProjectConfigFile.GetValue(LegacyName, null);
			if (NewValue != null)
			{
				Value = NewValue;
				return true;
			}

			Value = null;
			return false;
		}

		private List<BadgeInfo> CreateBuildBadges(int ChangeNumber, EventSummary? Summary)
		{
			List<BadgeInfo> Badges = new List<BadgeInfo>();

			if (Summary != null && Summary.Badges.Count > 0)
			{
				// Create a lookup for build data for each badge name
				Dictionary<string, BadgeData> BadgeNameToBuildData = new Dictionary<string, BadgeData>();
				foreach (BadgeData Badge in Summary.Badges)
				{
					BadgeNameToBuildData[Badge.BadgeName] = Badge;
				}

				// Add all the badges, sorted by group
				foreach (KeyValuePair<string, string> BadgeNameAndGroup in BadgeNameAndGroupPairs)
				{
					BadgeData? BadgeData;
					BadgeNameToBuildData.TryGetValue(BadgeNameAndGroup.Key, out BadgeData);

					BadgeInfo BadgeInfo = CreateBadge(ChangeNumber, BadgeNameAndGroup.Key, BadgeNameAndGroup.Value, BadgeData);
					if (MaxBuildBadgeChars != -1 && BadgeInfo.Label.Length > MaxBuildBadgeChars)
					{
						BadgeInfo.ToolTip = BadgeInfo.Label;
						BadgeInfo.Label = BadgeInfo.Label.Substring(0, MaxBuildBadgeChars);
					}
					Badges.Add(BadgeInfo);
				}
			}

			// Layout the badges
			LayoutBadges(Badges);
			return Badges;
		}

		private BadgeInfo CreateBadge(int ChangeNumber, string BadgeName, string BadgeGroup, BadgeData? BadgeData)
		{
			string BadgeLabel = BadgeName;
			Color BadgeColor = Color.FromArgb(0, Color.White);

			if (BadgeData != null)
			{
				BadgeLabel = BadgeData.BadgeLabel;
				BadgeColor = GetBuildBadgeColor(BadgeData.Result);
			}

			Color HoverBadgeColor = Color.FromArgb(BadgeColor.A, Math.Min(BadgeColor.R + 32, 255), Math.Min(BadgeColor.G + 32, 255), Math.Min(BadgeColor.B + 32, 255));

			Action? ClickHandler;
			if (BadgeData == null || String.IsNullOrEmpty(BadgeData.Url))
			{
				ClickHandler = null;
			}
			else
			{
				ClickHandler = () => SafeProcessStart(BadgeData.Url);
			}

			string UniqueId = String.Format("{0}:{1}", ChangeNumber, BadgeName);
			return new BadgeInfo(BadgeLabel, BadgeGroup, UniqueId, BadgeColor, HoverBadgeColor, ClickHandler);
		}

		private Dictionary<string, List<BadgeInfo>> CreateCustomBadges(int ChangeNumber, EventSummary? Summary)
		{
			Dictionary<string, List<BadgeInfo>> ColumnNameToBadges = new Dictionary<string, List<BadgeInfo>>();
			if (Summary != null && Summary.Badges.Count > 0)
			{
				foreach (ColumnHeader CustomColumn in CustomColumns)
				{
					ConfigObject Config = (ConfigObject)CustomColumn.Tag;
					if (Config != null)
					{
						List<BadgeInfo> Badges = new List<BadgeInfo>();

						string[] BadgeNames = Config.GetValue("Badges", "").Split(new char[] { ',' }, StringSplitOptions.RemoveEmptyEntries);
						foreach (string BadgeName in BadgeNames)
						{
							BadgeInfo Badge = CreateBadge(ChangeNumber, BadgeName, "XXXX", Summary.Badges.FirstOrDefault(x => x.BadgeName == BadgeName));
							Badges.Add(Badge);
						}

						LayoutBadges(Badges);

						ColumnNameToBadges[CustomColumn.Text] = Badges;
					}
				}
			}
			return ColumnNameToBadges;
		}

		private static Color GetBuildBadgeColor(BadgeResult Result)
		{
			if (Result == BadgeResult.Starting)
			{
				return Color.FromArgb(128, 192, 255);
			}
			else if (Result == BadgeResult.Warning)
			{
				return Color.FromArgb(255, 192, 0);
			}
			else if (Result == BadgeResult.Failure)
			{
				return Color.FromArgb(192, 64, 0);
			}
			else if (Result == BadgeResult.Skipped)
			{
				return Color.FromArgb(192, 192, 192);
			}
			else
			{
				return Color.FromArgb(128, 192, 64);
			}
		}

		private Rectangle GetSyncBadgeRectangle(Rectangle Bounds)
		{
			Size BadgeSize = GetBadgeSize("Sync");
			return new Rectangle(Bounds.Right - BadgeSize.Width - 2, Bounds.Top + (Bounds.Height - BadgeSize.Height) / 2, BadgeSize.Width, BadgeSize.Height);
		}

		private void DrawSingleBadge(Graphics Graphics, Rectangle DisplayRectangle, string BadgeText, Color BadgeColor)
		{
			Size BadgeSize = GetBadgeSize(BadgeText);

			int X = DisplayRectangle.Left + (DisplayRectangle.Width - BadgeSize.Width) / 2;
			int Y = DisplayRectangle.Top + (DisplayRectangle.Height - BadgeSize.Height) / 2;

			DrawBadge(Graphics, new Rectangle(X, Y, BadgeSize.Width, BadgeSize.Height), BadgeText, BadgeColor, false, false);
		}

		private void DrawBadge(Graphics Graphics, Rectangle BadgeRect, string BadgeText, Color BadgeColor, bool bMergeLeft, bool bMergeRight)
		{
			if (BadgeColor.A != 0)
			{
				using (GraphicsPath Path = new GraphicsPath())
				{
					Path.StartFigure();
					Path.AddLine(BadgeRect.Left + (bMergeLeft ? 1 : 0), BadgeRect.Top, BadgeRect.Left - (bMergeLeft ? 1 : 0), BadgeRect.Bottom);
					Path.AddLine(BadgeRect.Left - (bMergeLeft ? 1 : 0), BadgeRect.Bottom, BadgeRect.Right - 1 - (bMergeRight ? 1 : 0), BadgeRect.Bottom);
					Path.AddLine(BadgeRect.Right - 1 - (bMergeRight ? 1 : 0), BadgeRect.Bottom, BadgeRect.Right - 1 + (bMergeRight ? 1 : 0), BadgeRect.Top);
					Path.AddLine(BadgeRect.Right - 1 + (bMergeRight ? 1 : 0), BadgeRect.Top, BadgeRect.Left + (bMergeLeft ? 1 : 0), BadgeRect.Top);
					Path.CloseFigure();

					using (SolidBrush Brush = new SolidBrush(BadgeColor))
					{
						Graphics.FillPath(Brush, Path);
					}
				}

				TextRenderer.DrawText(Graphics, BadgeText, BadgeFont, BadgeRect, Color.White, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix | TextFormatFlags.PreserveGraphicsClipping);
			}
		}

		private Size GetBadgeSize(string BadgeText)
		{
			Size BadgeSize;
			if (!BadgeLabelToSize.TryGetValue(BadgeText, out BadgeSize))
			{
				Size LabelSize = TextRenderer.MeasureText(BadgeText, BadgeFont);
				int BadgeHeight = BadgeFont!.Height + 1;

				BadgeSize = new Size(LabelSize.Width + BadgeHeight - 4, BadgeHeight);
				BadgeLabelToSize[BadgeText] = BadgeSize;
			}
			return BadgeSize;
		}

		private static bool GetLastUpdateMessage(WorkspaceUpdateResult Result, string? ResultMessage, [NotNullWhen(true)] out string? Message)
		{
			if (Result != WorkspaceUpdateResult.Success && ResultMessage != null)
			{
				Message = ResultMessage;
				return true;
			}
			return GetGenericLastUpdateMessage(Result, out Message);
		}

		private static bool GetGenericLastUpdateMessage(WorkspaceUpdateResult Result, [NotNullWhen(true)] out string? Message)
		{
			switch (Result)
			{
				case WorkspaceUpdateResult.Canceled:
					Message = "Sync canceled.";
					return true;
				case WorkspaceUpdateResult.FailedToSync:
					Message = "Failed to sync files.";
					return true;
				case WorkspaceUpdateResult.FailedToSyncLoginExpired:
					Message = "Failed to sync files (login expired).";
					return true;
				case WorkspaceUpdateResult.FilesToResolve:
					Message = "Sync finished with unresolved files.";
					return true;
				case WorkspaceUpdateResult.FilesToClobber:
					Message = "Sync failed due to modified files in workspace.";
					return true;
				case WorkspaceUpdateResult.FilesToDelete:
					Message = "Sync aborted pending confirmation of files to delete.";
					return true;
				case WorkspaceUpdateResult.FailedToCompile:
				case WorkspaceUpdateResult.FailedToCompileWithCleanWorkspace:
					Message = "Compile failed.";
					return true;
				default:
					Message = null;
					return false;
			}
		}

		public static string FormatUserName(string UserName)
		{
			if (UserName == null)
			{
				return "(Invalid Name)";
			}
			else
			{
				return Utility.FormatUserName(UserName);
			}
		}

		private static void AppendUserList(StringBuilder Builder, string Separator, string Format, IEnumerable<EventData> Reviews)
		{
			AppendItemList(Builder, Separator, Format, Reviews.Select(x => FormatUserName(x.UserName)));
		}

		private static void AppendItemList(StringBuilder Builder, string Separator, string Format, IEnumerable<string> Items)
		{
			string? ItemList = FormatItemList(Format, Items);
			if (ItemList != null)
			{
				if (Builder.Length > 0)
				{
					Builder.Append(Separator);
				}
				Builder.Append(ItemList);
			}
		}

		private static string? FormatItemList(string Format, IEnumerable<string> Items)
		{
			string[] ItemsArray = Items.Distinct().ToArray();
			if (ItemsArray.Length == 0)
			{
				return null;
			}

			StringBuilder Builder = new StringBuilder(ItemsArray[0]);
			if (ItemsArray.Length > 1)
			{
				for (int Idx = 1; Idx < ItemsArray.Length - 1; Idx++)
				{
					Builder.Append(", ");
					Builder.Append(ItemsArray[Idx]);
				}
				Builder.Append(" and ");
				Builder.Append(ItemsArray.Last());
			}
			return String.Format(Format, Builder.ToString());
		}

		private void BuildList_MouseDoubleClick(object Sender, MouseEventArgs Args)
		{
			if (Args.Button == MouseButtons.Left)
			{
				ListViewHitTestInfo HitTest = BuildList.HitTest(Args.Location);
				if (HitTest.Item != null)
				{
					ChangesRecord Change = (ChangesRecord)HitTest.Item.Tag;
					if (Change != null)
					{
						if (Change.Number == Workspace.CurrentChangeNumber)
						{
							LaunchEditor();
						}
						else
						{
							StartSync(Change.Number, false, null);
						}
					}
				}
			}
		}

		public void LaunchEditor()
		{
			if (!Workspace.IsBusy() && Workspace.CurrentChangeNumber != -1)
			{
				BuildConfig EditorBuildConfig = GetEditorBuildConfig();

				FileReference ReceiptFile = ConfigUtils.GetEditorReceiptFile(Workspace.Project, Workspace.ProjectConfigFile, EditorBuildConfig);
				if (ConfigUtils.TryReadEditorReceipt(Workspace.Project, ReceiptFile, out TargetReceipt? Receipt) && Receipt.Launch != null && File.Exists(Receipt.Launch))
				{
					if (Settings.bEditorArgumentsPrompt && !ModifyEditorArguments())
					{
						return;
					}

					StringBuilder LaunchArguments = new StringBuilder();
					if (SelectedFileName.HasExtension(".uproject"))
					{
						LaunchArguments.AppendFormat("\"{0}\"", SelectedFileName);
					}
					foreach (Tuple<string, bool> EditorArgument in Settings.EditorArguments)
					{
						if (EditorArgument.Item2)
						{
							LaunchArguments.AppendFormat(" {0}", EditorArgument.Item1);
						}
					}
					if (EditorBuildConfig == BuildConfig.Debug || EditorBuildConfig == BuildConfig.DebugGame)
					{
						LaunchArguments.Append(" -debug");
					}

					if (!Utility.SpawnProcess(Receipt.Launch, LaunchArguments.ToString()))
					{
						ShowErrorDialog("Unable to spawn {0} {1}", Receipt.Launch, LaunchArguments.ToString());
					}
				}
				else
				{
					if (MessageBox.Show("The editor needs to be built before you can run it. Build it now?", "Editor out of date", MessageBoxButtons.YesNo) == System.Windows.Forms.DialogResult.Yes)
					{
						Owner.ShowAndActivate();

						WorkspaceUpdateOptions Options = WorkspaceUpdateOptions.Build | WorkspaceUpdateOptions.RunAfterSync;
						WorkspaceUpdateContext Context = new WorkspaceUpdateContext(Workspace.CurrentChangeNumber, Options, Settings.CompiledEditorBuildConfig, null, ProjectSettings.BuildSteps, null);
						StartWorkspaceUpdate(Context, null);
					}
				}
			}
		}

		private bool WaitForProgramsToFinish()
		{
			FileReference[] ProcessFileNames = GetProcessesRunningInWorkspace();
			if (ProcessFileNames.Length > 0)
			{
				ProgramsRunningWindow ProgramsRunning = new ProgramsRunningWindow(GetProcessesRunningInWorkspace, ProcessFileNames);
				if (ProgramsRunning.ShowDialog() != DialogResult.OK)
				{
					return false;
				}
			}
			return true;
		}

		private FileReference[] GetProcessesRunningInWorkspace()
		{
			HashSet<string> ProcessNames = new HashSet<string>(StringComparer.InvariantCultureIgnoreCase);
			ProcessNames.Add("Win64\\UE4Editor.exe");
			ProcessNames.Add("Win64\\UE4Editor-Cmd.exe");
			ProcessNames.Add("Win64\\UE4Editor-Win64-Debug.exe");
			ProcessNames.Add("Win64\\UE4Editor-Win64-Debug-Cmd.exe");
			ProcessNames.Add("Win64\\UE4Editor-Win64-DebugGame.exe");
			ProcessNames.Add("Win64\\UE4Editor-Win64-DebugGame-Cmd.exe");
			ProcessNames.Add("Win64\\UnrealEditor.exe");
			ProcessNames.Add("Win64\\UnrealEditor-Cmd.exe");
			ProcessNames.Add("Win64\\UnrealEditor-Win64-Debug.exe");
			ProcessNames.Add("Win64\\UnrealEditor-Win64-Debug-Cmd.exe");
			ProcessNames.Add("Win64\\UnrealEditor-Win64-DebugGame.exe");
			ProcessNames.Add("Win64\\UnrealEditor-Win64-DebugGame-Cmd.exe");
			ProcessNames.Add("Win64\\CrashReportClient.exe");
			ProcessNames.Add("Win64\\CrashReportClient-Win64-Development.exe");
			ProcessNames.Add("Win64\\CrashReportClient-Win64-Debug.exe");
			ProcessNames.Add("Win64\\CrashReportClientEditor.exe");
			ProcessNames.Add("Win64\\CrashReportClientEditor-Win64-Development.exe");
			ProcessNames.Add("Win64\\CrashReportClientEditor-Win64-Debug.exe");
			ProcessNames.Add("DotNET\\UnrealBuildTool\\UnrealBuildTool.exe");
			ProcessNames.Add("DotNET\\AutomationTool\\AutomationTool.exe");

			List<FileReference> ProcessFileNames = new List<FileReference>();
			try
			{
				DirectoryReference BinariesRootPath = DirectoryReference.Combine(Workspace.Project.LocalRootPath, "Engine\\Binaries");

				foreach (string ProcessName in ProcessNames)
				{
					try
					{
						FileReference ProcessFilename = FileReference.Combine(BinariesRootPath, ProcessName);

						if (FileReference.Exists(ProcessFilename))
						{
							try
							{
								// Try to open the file to determine whether the executable is running.
								// We do this so that we can also find processes running under other user sessions, without needing PROCESS_QUERY_INFORMATION access rights.
								using FileStream TestStream = File.OpenWrite(ProcessFilename.FullName);
							}
							catch (IOException)
							{
								// This file is in use, so add it to the list of running processes
								ProcessFileNames.Add(ProcessFilename);
							}
						}
					}
					catch
					{
					}
				}
			}
			catch
			{
			}
			return ProcessFileNames.ToArray();
		}

		private void TimerCallback(object? Sender, EventArgs Args)
		{
			Tuple<string, float> Progress = Workspace.CurrentProgress;
			if (Progress != null && Progress.Item2 > 0.0f)
			{
				DesiredTaskbarState = Tuple.Create(TaskbarState.Normal, Progress.Item2);
			}
			else
			{
				DesiredTaskbarState = Tuple.Create(TaskbarState.NoProgress, 0.0f);
			}

			Owner.UpdateProgress();

			UpdateStatusPanel();
			BuildList.Refresh();
		}

		private string? GetDefaultIssueFilter()
		{
			string? BuildHealthProject;
			if (TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "BuildHealthProject", out BuildHealthProject))
			{
				return BuildHealthProject;
			}

			string? DefaultIssueFilter;
			if (TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "DefaultIssueFilter", out DefaultIssueFilter))
			{
				return DefaultIssueFilter;
			}

			return null;
		}

		private Dictionary<string, Func<IssueData, bool>> GetCustomIssueFilters()
		{
			Dictionary<string, Func<IssueData, bool>> CustomFilters = new Dictionary<string, Func<IssueData, bool>>();

			string? BuildHealthProject;
			if (TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "BuildHealthProject", out BuildHealthProject))
			{
				CustomFilters[BuildHealthProject] = x => !String.IsNullOrEmpty(x.Project) && x.Project.Equals(BuildHealthProject, StringComparison.OrdinalIgnoreCase);
			}

			string[]? Filters;
			if (TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "IssueFilters", out Filters))
			{
				foreach (string Filter in Filters)
				{
					try
					{
						ConfigObject Config = new ConfigObject(Filter);

						string? Name = Config.GetValue("Name", null);
						string? Pattern = Config.GetValue("Pattern", null);

						if(Name != null && Pattern != null && StreamName != null)
						{
							string? DepotName;
							if (PerforceUtils.TryGetDepotName(StreamName, out DepotName))
							{
								Pattern = Regex.Replace(Pattern, @"\$\(Depot\)", DepotName, RegexOptions.IgnoreCase);
							}
							Pattern = Regex.Replace(Pattern, @"\$\(Stream\)", StreamName, RegexOptions.IgnoreCase);

							Regex PatternRegex = new Regex(String.Format("^(?:{0})$", Pattern), RegexOptions.IgnoreCase);
							CustomFilters[Name] = x => x.Streams.Any(y => PatternRegex.IsMatch(y));
						}
					}
					catch(Exception Ex)
					{
						Logger.LogError(Ex, "Unable to parse config filter '{Filter}'", Filter);
					}
				}
			}

			return CustomFilters;
		}

		private void OpenPerforce()
		{
			StringBuilder CommandLine = new StringBuilder();
			if (Workspace != null)
			{
				CommandLine.Append(PerforceSettings.GetArgumentsForExternalProgram(true));
			}
			SafeProcessStart("p4v.exe", CommandLine.ToString());
		}


		void RunTool(ToolDefinition Tool, ToolLink Link)
		{
			DirectoryReference? ToolDir = Owner.ToolUpdateMonitor.GetToolPath(Tool.Name);
			if (ToolDir != null)
			{
				Dictionary<string, string> Variables = Workspace.GetVariables(GetEditorBuildConfig());
				Variables["ToolDir"] = ToolDir.FullName;

				string FileName = Utility.ExpandVariables(Link.FileName, Variables);
				string Arguments = Utility.ExpandVariables(Link.Arguments ?? String.Empty, Variables);
				string WorkingDir = Utility.ExpandVariables(Link.WorkingDir ?? ToolDir.FullName, Variables);

				SafeProcessStart(FileName, Arguments, WorkingDir);
			}
		}

		private void UpdateStatusPanel_CrossThread()
		{
			MainThreadSynchronizationContext.Post((o) => { if (!IsDisposed && !bIsDisposing) { UpdateStatusPanel(); } }, null);
		}

		private void UpdateStatusPanel()
		{
			if(Workspace == null)
			{
				return;
			}

			int NewContentWidth = Math.Max(TextRenderer.MeasureText(String.Format("Opened {0}  |  Browse...  |  Connect...", SelectedFileName), StatusPanel.Font).Width, 400);
			StatusPanel.SetContentWidth(NewContentWidth);

			List<StatusLine> Lines = new List<StatusLine>();
			if (Workspace.IsBusy())
			{
				// Sync in progress
				Tuple<string, float> Progress = Workspace.CurrentProgress;

				StatusLine SummaryLine = new StatusLine();
				if (Workspace.PendingChangeNumber == -1)
				{
					SummaryLine.AddText("Working... | ");
				}
				else
				{
					SummaryLine.AddText("Syncing to changelist ");
					SummaryLine.AddLink(Workspace.PendingChangeNumber.ToString(), FontStyle.Regular, () => { SelectChange(Workspace.PendingChangeNumber); });
					SummaryLine.AddText("... | ");
				}
				SummaryLine.AddLink(Splitter.IsLogVisible() ? "Hide Log" : "Show Log", FontStyle.Bold | FontStyle.Underline, () => { ToggleLogVisibility(); });
				SummaryLine.AddText(" | ");
				SummaryLine.AddLink("Cancel", FontStyle.Bold | FontStyle.Underline, () => { CancelWorkspaceUpdate(); });
				Lines.Add(SummaryLine);

				StatusLine ProgressLine = new StatusLine();
				ProgressLine.AddText(String.Format("{0}  ", Progress.Item1));
				if (Progress.Item2 > 0.0f) ProgressLine.AddProgressBar(Progress.Item2);
				Lines.Add(ProgressLine);
			}
			else
			{
				// Project
				StatusLine ProjectLine = new StatusLine();
				ProjectLine.AddText(String.Format("Opened "));
				ProjectLine.AddLink(SelectedFileName.FullName + " \u25BE", FontStyle.Regular, (P, R) => { SelectRecentProject(R); });
				ProjectLine.AddText("  |  ");
				ProjectLine.AddLink("Settings...", FontStyle.Regular, (P, R) => { Owner.EditSelectedProject(this); });
				Lines.Add(ProjectLine);

				// Spacer
				Lines.Add(new StatusLine() { LineHeight = 0.5f });

				// Sync status
				StatusLine SummaryLine = new StatusLine();
				if (IsBisectModeEnabled())
				{
					int PassChangeNumber;
					int FailChangeNumber;
					GetRemainingBisectRange(out PassChangeNumber, out FailChangeNumber);

					SummaryLine.AddText("Bisecting changes between ");
					SummaryLine.AddLink(String.Format("{0}", PassChangeNumber), FontStyle.Regular, () => { SelectChange(PassChangeNumber); });
					SummaryLine.AddText(" and ");
					SummaryLine.AddLink(String.Format("{0}", FailChangeNumber), FontStyle.Regular, () => { SelectChange(FailChangeNumber); });
					SummaryLine.AddText(".  ");

					int BisectChangeNumber = GetBisectChangeNumber();
					if (BisectChangeNumber != -1)
					{
						SummaryLine.AddLink("Sync Next", FontStyle.Bold | FontStyle.Underline, () => { SyncBisectChange(); });
						SummaryLine.AddText(" | ");
					}

					SummaryLine.AddLink("Cancel", FontStyle.Bold | FontStyle.Underline, () => { CancelBisectMode(); });
				}
				else
				{
					if (Workspace.CurrentChangeNumber != -1)
					{
						if (StreamName == null)
						{
							SummaryLine.AddText("Last synced to changelist ");
						}
						else
						{
							SummaryLine.AddText("Last synced to ");
							if (Workspace.ProjectStreamFilter == null || Workspace.ProjectStreamFilter.Count == 0)
							{
								SummaryLine.AddLink(StreamName, FontStyle.Regular, (P, R) => { SelectOtherStreamDialog(); });
							}
							else
							{
								SummaryLine.AddLink(StreamName + "\u25BE", FontStyle.Regular, (P, R) => { SelectOtherStream(R); });
							}
							SummaryLine.AddText(" at changelist ");
						}
						SummaryLine.AddLink(String.Format("{0}.", Workspace.CurrentChangeNumber), FontStyle.Regular, () => { SelectChange(Workspace.CurrentChangeNumber); });
					}
					else
					{
						SummaryLine.AddText("You are not currently synced to ");
						if (StreamName == null)
						{
							SummaryLine.AddText("this branch.");
						}
						else
						{
							SummaryLine.AddLink(StreamName + " \u25BE", FontStyle.Regular, (P, R) => { SelectOtherStream(R); });
						}
					}
					SummaryLine.AddText("  |  ");
					SummaryLine.AddLink("Sync Now", FontStyle.Bold | FontStyle.Underline, () => { SyncLatestChange(); });
					SummaryLine.AddText(" - ");
					SummaryLine.AddLink(" To... \u25BE", 0, (P, R) => { ShowSyncMenu(R); });
				}
				Lines.Add(SummaryLine);

				// Programs
				StatusLine ProgramsLine = new StatusLine();
				if (Workspace.CurrentChangeNumber != -1)
				{
					ProgramsLine.AddLink("Unreal Editor", FontStyle.Regular, () => { LaunchEditor(); });
					ProgramsLine.AddText("  |  ");
				}

				string[]? SdkInfoEntries;
				if (TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "SdkInfo", out SdkInfoEntries))
				{
					ProgramsLine.AddLink("SDK Info", FontStyle.Regular, () => { ShowRequiredSdkInfo(); });
					ProgramsLine.AddText("  |  ");
				}

				ProgramsLine.AddLink("Perforce", FontStyle.Regular, () => { OpenPerforce(); });
				ProgramsLine.AddText("  |  ");
				if (!ShouldSyncPrecompiledEditor)
				{
					ProgramsLine.AddLink("Visual Studio", FontStyle.Regular, () => { OpenSolution(); });
					ProgramsLine.AddText("  |  ");
				}

				List<ToolDefinition> Tools = Owner.ToolUpdateMonitor.Tools;
				foreach(ToolDefinition Tool in Tools)
				{
					if (Tool.Enabled)
					{
						foreach (ToolLink Link in Tool.StatusPanelLinks)
						{
							ProgramsLine.AddLink(Link.Label, FontStyle.Regular, () => RunTool(Tool, Link));
							ProgramsLine.AddText("  |  ");
						}
					}
				}

				foreach ((string Name, Action<Point, Rectangle> Action) in CustomStatusPanelLinks)
				{
					ProgramsLine.AddLink(Name, FontStyle.Regular, Action);
					ProgramsLine.AddText("  |  ");
				}

				ProgramsLine.AddLink("Windows Explorer", FontStyle.Regular, () => { SafeProcessStart("explorer.exe", String.Format("\"{0}\"", SelectedFileName.Directory.FullName)); });

				if (GetDefaultIssueFilter() != null)
				{
					ProgramsLine.AddText("  |  ");
					if (bUserHasOpenIssues)
					{
						ProgramsLine.AddBadge("Build Health", GetBuildBadgeColor(BadgeResult.Failure), (P, R) => ShowBuildHealthMenu(R));
					}
					else
					{
						ProgramsLine.AddLink("Build Health", FontStyle.Regular, (P, R) => { ShowBuildHealthMenu(R); });
					}
				}

				foreach (ServiceBadgeInfo ServiceBadge in ServiceBadges)
				{
					ProgramsLine.AddText("  |  ");
					if (ServiceBadge.Url == null)
					{
						ProgramsLine.AddBadge(ServiceBadge.Name, GetBuildBadgeColor(ServiceBadge.Result), null);
					}
					else
					{
						ProgramsLine.AddBadge(ServiceBadge.Name, GetBuildBadgeColor(ServiceBadge.Result), (P, R) => { SafeProcessStart(ServiceBadge.Url); });
					}
				}
				ProgramsLine.AddText("  |  ");
				ProgramsLine.AddLink("More... \u25BE", FontStyle.Regular, (P, R) => { ShowActionsMenu(R); });
				Lines.Add(ProgramsLine);

				// Get the summary of the last sync
				if (WorkspaceState.LastSyncChangeNumber > 0)
				{
					string? SummaryText;
					if (WorkspaceState.LastSyncChangeNumber == Workspace.CurrentChangeNumber && WorkspaceState.LastSyncResult == WorkspaceUpdateResult.Success && WorkspaceState.LastSyncTime.HasValue)
					{
						Lines.Add(new StatusLine() { LineHeight = 0.5f });

						StatusLine SuccessLine = new StatusLine();
						SuccessLine.AddIcon(Properties.Resources.StatusIcons, new Size(16, 16), 0);
						SuccessLine.AddText(String.Format("  Sync took {0}{1}s, completed at {2}.", (WorkspaceState.LastSyncDurationSeconds >= 60) ? String.Format("{0}m ", WorkspaceState.LastSyncDurationSeconds / 60) : "", WorkspaceState.LastSyncDurationSeconds % 60, WorkspaceState.LastSyncTime.Value.ToLocalTime().ToString("h\\:mmtt").ToLowerInvariant()));
						Lines.Add(SuccessLine);
					}
					else if (GetLastUpdateMessage(WorkspaceState.LastSyncResult, WorkspaceState.LastSyncResultMessage, out SummaryText))
					{
						Lines.Add(new StatusLine() { LineHeight = 0.5f });

						int SummaryTextLength = SummaryText.IndexOf('\n');
						if (SummaryTextLength == -1)
						{
							SummaryTextLength = SummaryText.Length;
						}
						SummaryTextLength = Math.Min(SummaryTextLength, 80);

						StatusLine FailLine = new StatusLine();
						FailLine.AddIcon(Properties.Resources.StatusIcons, new Size(16, 16), 1);

						if (SummaryTextLength == SummaryText.Length)
						{
							FailLine.AddText(String.Format("  {0}  ", SummaryText));
						}
						else
						{
							FailLine.AddText(String.Format("  {0}...  ", SummaryText.Substring(0, SummaryTextLength).TrimEnd()));
							FailLine.AddLink("More...", FontStyle.Bold | FontStyle.Underline, () => { ViewLastSyncStatus(); });
							FailLine.AddText("  |  ");
						}
						FailLine.AddLink("Show Log", FontStyle.Bold | FontStyle.Underline, () => { ShowErrorInLog(); });
						Lines.Add(FailLine);
					}
				}
			}

			StatusLine? Caption = null;
			if (StreamName != null && !Workspace.IsBusy())
			{
				Caption = new StatusLine();
				Caption.AddLink(StreamName + "\u25BE", FontStyle.Bold, (P, R) => { SelectOtherStream(R); });
			}

			StatusLine? Alert = null;
			Color? TintColor = null;

			ConfigFile ProjectConfigFile = PerforceMonitor.LatestProjectConfigFile;
			if (ProjectConfigFile != null)
			{
				string? Message;
				if (TryGetProjectSetting(ProjectConfigFile, "Message", out Message))
				{
					Alert = CreateStatusLineFromMarkdown(Message);
				}

				string? StatusPanelColor;
				if (TryGetProjectSetting(ProjectConfigFile, "StatusPanelColor", out StatusPanelColor))
				{
					TintColor = System.Drawing.ColorTranslator.FromHtml(StatusPanelColor);
				}
			}

			using (Graphics Graphics = CreateGraphics())
			{
				int StatusPanelHeight = (int)(148.0f * Graphics.DpiY / 96.0f);
				if (Alert != null)
				{
					StatusPanelHeight += (int)(40.0f * Graphics.DpiY / 96.0f);
				}
				if (StatusLayoutPanel.RowStyles[0].Height != StatusPanelHeight)
				{
					StatusLayoutPanel.RowStyles[0].Height = StatusPanelHeight;
				}
			}

			StatusPanel.Set(Lines, Caption, Alert, TintColor);
		}

		private void ShowBuildHealthMenu(Rectangle Bounds)
		{
			int MinSeparatorIdx = BuildHealthContextMenu.Items.IndexOf(BuildHealthContextMenu_MinSeparator);

			while (BuildHealthContextMenu.Items[MinSeparatorIdx + 1] != BuildHealthContextMenu_MaxSeparator)
			{
				BuildHealthContextMenu.Items.RemoveAt(MinSeparatorIdx + 1);
			}

			Func<IssueData, bool>? Predicate = null;
			string? DefaultIssueFilter = GetDefaultIssueFilter();
			if (DefaultIssueFilter == null || !GetCustomIssueFilters().TryGetValue(DefaultIssueFilter, out Predicate))
			{
				Predicate = x => true;
			}

			List<IssueData> Issues = new List<IssueData>();
			bool bHasOtherAssignedIssue = false;
			foreach (IssueData Issue in IssueMonitor.GetIssues().OrderByDescending(x => x.Id))
			{
				if (Predicate(Issue))
				{
					Issues.Add(Issue);
				}
				else if (Issue.FixChange == 0 && Issue.Owner != null && String.Compare(Issue.Owner, PerforceSettings.UserName, StringComparison.OrdinalIgnoreCase) == 0)
				{
					bHasOtherAssignedIssue = true;
				}
			}

			BuildHealthContextMenu_Browse.Font = new Font(BuildHealthContextMenu_Browse.Font, bHasOtherAssignedIssue ? FontStyle.Bold : FontStyle.Regular);

			if (Issues.Count == 0)
			{
				BuildHealthContextMenu_MaxSeparator.Visible = false;
			}
			else
			{
				BuildHealthContextMenu_MaxSeparator.Visible = true;
				for (int Idx = 0; Idx < Issues.Count; Idx++)
				{
					IssueData Issue = Issues[Idx];

					string Summary = Issue.Summary;
					if (Summary.Length > 100)
					{
						Summary = Summary.Substring(0, 100).TrimEnd() + "...";
					}

					StringBuilder Description = new StringBuilder();
					Description.AppendFormat("{0}: {1}", Issue.Id, Summary);
					if (Issue.Owner == null)
					{
						Description.AppendFormat(" - Unassigned");
					}
					else
					{
						Description.AppendFormat(" - {0}", FormatUserName(Issue.Owner));
						if (!Issue.AcknowledgedAt.HasValue)
						{
							Description.Append(" (?)");
						}
					}
					if (Issue.FixChange > 0)
					{
						Description.AppendFormat(" (Unverified fix in CL {0})", Issue.FixChange);
					}

					Description.AppendFormat(" ({0})", Utility.FormatDurationMinutes((int)((Issue.RetrievedAt - Issue.CreatedAt).TotalMinutes + 1)));

					ToolStripMenuItem Item = new ToolStripMenuItem(Description.ToString());
					if (Issue.FixChange > 0)
					{
						Item.ForeColor = SystemColors.GrayText;
					}
					else if (Issue.Owner != null && String.Compare(Issue.Owner, PerforceSettings.UserName, StringComparison.OrdinalIgnoreCase) == 0)
					{
						Item.Font = new Font(Item.Font, FontStyle.Bold);
					}
					Item.Click += (s, e) => { BuildHealthContextMenu_Issue_Click(Issue); };
					BuildHealthContextMenu.Items.Insert(MinSeparatorIdx + Idx + 1, Item);
				}
			}

			BuildHealthContextMenu.Show(StatusPanel, new Point(Bounds.Right, Bounds.Bottom), ToolStripDropDownDirection.BelowLeft);
		}

		private void BuildHealthContextMenu_Issue_Click(IssueData Issue)
		{
			ShowIssueDetails(Issue);
		}

		private TimeSpan? GetServerTimeOffset()
		{
			TimeSpan? Offset = null;
			if (Settings.bShowLocalTimes)
			{
				Offset = PerforceMonitor.ServerTimeZone;
			}
			return Offset;
		}

		public void ShowIssueDetails(IssueData Issue)
		{
			IssueDetailsWindow.Show(ParentForm, IssueMonitor, PerforceSettings, GetServerTimeOffset(), Issue, ServiceProvider, StreamName);
		}

		private void BuildHealthContextMenu_Browse_Click(object sender, EventArgs e)
		{
			string? DefaultFilter = GetDefaultIssueFilter();
			Dictionary<string, Func<IssueData, bool>> CustomFilters = GetCustomIssueFilters();
			IssueBrowserWindow.Show(ParentForm, IssueMonitor, PerforceSettings, GetServerTimeOffset(), ServiceProvider, StreamName, CustomFilters, DefaultFilter);
		}

		private void BuildHealthContextMenu_Settings_Click(object sender, EventArgs e)
		{
			string? BuildHealthProject;
			TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "BuildHealthProject", out BuildHealthProject);

			IssueSettingsWindow IssueSettings = new IssueSettingsWindow(Settings, BuildHealthProject ?? "");
			if (IssueSettings.ShowDialog(this) == DialogResult.OK)
			{
				Owner.UpdateAlertWindows();
			}
		}

		private void ShowRequiredSdkInfo()
		{
			string[]? SdkInfoEntries;
			if (TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "SdkInfo", out SdkInfoEntries))
			{
				Dictionary<string, string> Variables = Workspace.GetVariables(GetEditorBuildConfig());
				SdkInfoWindow Window = new SdkInfoWindow(SdkInfoEntries, Variables, BadgeFont!);
				Window.ShowDialog();
			}
		}

		private StatusLine CreateStatusLineFromMarkdown(string Text)
		{
			StatusLine Line = new StatusLine();

			FontStyle Style = FontStyle.Regular;

			StringBuilder ElementText = new StringBuilder();
			for (int Idx = 0; Idx < Text.Length;)
			{
				// Bold and italic
				if (Text[Idx] == '_' || Text[Idx] == '*')
				{
					if (Idx + 1 < Text.Length && Text[Idx + 1] == Text[Idx])
					{
						FlushMarkdownText(Line, ElementText, Style);
						Idx += 2;
						Style ^= FontStyle.Bold;
						continue;
					}
					else
					{
						FlushMarkdownText(Line, ElementText, Style);
						Idx++;
						Style ^= FontStyle.Italic;
						continue;
					}
				}

				// Strikethrough
				if (Idx + 2 < Text.Length && Text[Idx] == '~' && Text[Idx + 1] == '~')
				{
					FlushMarkdownText(Line, ElementText, Style);
					Idx += 2;
					Style ^= FontStyle.Strikeout;
					continue;
				}

				// Link
				if (Text[Idx] == '[')
				{
					if (Idx + 1 < Text.Length && Text[Idx + 1] == '[')
					{
						ElementText.Append(Text[Idx]);
						Idx += 2;
						continue;
					}

					int EndIdx = Text.IndexOf("](", Idx);
					if (EndIdx != -1)
					{
						int UrlEndIdx = Text.IndexOf(')', EndIdx + 2);
						if (UrlEndIdx != -1)
						{
							FlushMarkdownText(Line, ElementText, Style);
							string LinkText = Text.Substring(Idx + 1, EndIdx - (Idx + 1));
							string LinkUrl = Text.Substring(EndIdx + 2, UrlEndIdx - (EndIdx + 2));
							Line.AddLink(LinkText, Style, () => SafeProcessStart(LinkUrl));
							Idx = UrlEndIdx + 1;
							continue;
						}
					}
				}

				// Icon
				if (Text[Idx] == ':')
				{
					int EndIdx = Text.IndexOf(':', Idx + 1);
					if (EndIdx != -1)
					{
						if (String.Compare(":alert:", 0, Text, Idx, EndIdx - Idx) == 0)
						{
							FlushMarkdownText(Line, ElementText, Style);
							Line.AddIcon(Properties.Resources.StatusIcons, new Size(16, 16), 4);
							Idx = EndIdx + 1;
							continue;
						}
					}
				}

				// Otherwise, just append the current character
				ElementText.Append(Text[Idx++]);
			}
			FlushMarkdownText(Line, ElementText, Style);

			return Line;
		}

		private void FlushMarkdownText(StatusLine Line, StringBuilder ElementText, FontStyle Style)
		{
			if (ElementText.Length > 0)
			{
				Line.AddText(ElementText.ToString(), Style);
				ElementText.Clear();
			}
		}

		private void SelectOtherStream(Rectangle Bounds)
		{
			bool bShownContextMenu = false;
			if (StreamName != null)
			{
				IReadOnlyList<string>? OtherStreamNames = Workspace.ProjectStreamFilter;
				if (OtherStreamNames != null)
				{
					StreamContextMenu.Items.Clear();

					ToolStripMenuItem CurrentStreamItem = new ToolStripMenuItem(StreamName, null, new EventHandler((S, E) => SelectStream(StreamName)));
					CurrentStreamItem.Checked = true;
					StreamContextMenu.Items.Add(CurrentStreamItem);

					StreamContextMenu.Items.Add(new ToolStripSeparator());

					foreach (string OtherStreamName in OtherStreamNames.OrderBy(x => x).Where(x => !x.EndsWith("/Dev-Binaries")))
					{
						string ThisStreamName = OtherStreamName; // Local for lambda capture
						if (String.Compare(StreamName, OtherStreamName, StringComparison.InvariantCultureIgnoreCase) != 0)
						{
							ToolStripMenuItem Item = new ToolStripMenuItem(ThisStreamName, null, new EventHandler((S, E) => SelectStream(ThisStreamName)));
							StreamContextMenu.Items.Add(Item);
						}
					}

					if (StreamContextMenu.Items.Count > 2)
					{
						StreamContextMenu.Items.Add(new ToolStripSeparator());
					}

					StreamContextMenu.Items.Add(new ToolStripMenuItem("Select Other...", null, new EventHandler((S, E) => SelectOtherStreamDialog())));

					int X = (Bounds.Left + Bounds.Right) / 2 + StreamContextMenu.Bounds.Width / 2;
					int Y = Bounds.Bottom + 2;
					StreamContextMenu.Show(StatusPanel, new Point(X, Y), ToolStripDropDownDirection.Left);

					bShownContextMenu = true;
				}
			}
			if (!bShownContextMenu)
			{
				SelectOtherStreamDialog();
			}
		}

		private void SelectOtherStreamDialog()
		{
			string? NewStreamName;
			if (SelectStreamWindow.ShowModal(this, PerforceSettings, StreamName, ServiceProvider, out NewStreamName))
			{
				SelectStream(NewStreamName);
			}
		}

		private void SelectStream(string NewStreamName)
		{
			if (StreamName != NewStreamName)
			{
				if (Workspace.IsBusy())
				{
					MessageBox.Show("Please retry after the current sync has finished.", "Sync in Progress");
				}
				else
				{
					for (int Idx = 0; Idx < 2; Idx++)
					{
						Func<IPerforceConnection, CancellationToken, Task<bool>> SwitchFunc = async (Perforce, CancellationToken) =>
						{
							if (Idx == 1 || !await Perforce.OpenedAsync(OpenedOptions.None, FileSpecList.Any, CancellationToken).AnyAsync())
							{
								await Perforce.SwitchClientToStreamAsync(NewStreamName, SwitchClientOptions.IgnoreOpenFiles, CancellationToken);
								return true;
							}
							return false;
						};

						ModalTask<bool>? SwitchTask = PerforceModalTask.Execute<bool>(this, "Switching streams", "Please wait...", PerforceSettings, SwitchFunc, Logger);
						if (SwitchTask == null || !SwitchTask.Succeeded)
						{
							break;
						}
						if (SwitchTask.Result)
						{
							StatusPanel.SuspendLayout();
							StreamChanged();
							StatusPanel.ResumeLayout();
							break;
						}
					}
				}
			}
		}

		private void ViewLastSyncStatus()
		{
			string? SummaryText;
			if (GetLastUpdateMessage(WorkspaceState.LastSyncResult, WorkspaceState.LastSyncResultMessage, out SummaryText))
			{
				string? CaptionText;
				if (!GetGenericLastUpdateMessage(WorkspaceState.LastSyncResult, out CaptionText))
				{
					CaptionText = "Sync error";
				}
				MessageBox.Show(SummaryText, CaptionText);
			}
		}

		private void ShowErrorInLog()
		{
			if (!Splitter.IsLogVisible())
			{
				ToggleLogVisibility();
			}
			SyncLog.ScrollToEnd();
		}

		private void ShowToolContextMenu(Rectangle Bounds, ContextMenuStrip MenuStrip)
		{
			MenuStrip.Show(StatusPanel, new Point(Bounds.Left, Bounds.Bottom), ToolStripDropDownDirection.BelowRight);
		}

		private void ShowActionsMenu(Rectangle Bounds)
		{
			MoreToolsContextMenu.Show(StatusPanel, new Point(Bounds.Left, Bounds.Bottom), ToolStripDropDownDirection.BelowRight);
		}

		private void SelectChange(int ChangeNumber)
		{
			BuildList.SelectedItems.Clear();

			PendingSelectedChangeNumber = -1;

			foreach (ListViewItem? Item in BuildList.Items)
			{
				if (Item != null)
				{
					ChangesRecord Summary = (ChangesRecord)Item.Tag;
					if (Summary != null && Summary.Number <= ChangeNumber)
					{
						Item.Selected = true;
						Item.EnsureVisible();
						return;
					}
				}
			}

			PendingSelectedChangeNumber = ChangeNumber;

			int CurrentMaxChanges = PerforceMonitor.CurrentMaxChanges;
			if (PerforceMonitor.PendingMaxChanges <= CurrentMaxChanges && BuildList.SelectedItems.Count == 0)
			{
				PerforceMonitor.PendingMaxChanges = CurrentMaxChanges + BuildListExpandCount;
				if (ExpandItem != null)
				{
					ExpandItem.EnsureVisible();
				}
			}
		}

		private void BuildList_MouseClick(object Sender, MouseEventArgs Args)
		{
			if (Args.Button == MouseButtons.Left)
			{
				ListViewHitTestInfo HitTest = BuildList.HitTest(Args.Location);
				if (HitTest.Item != null)
				{
					if (HitTest.Item == ExpandItem)
					{
						if (HitTestExpandLink(Args.Location))
						{
							int CurrentMaxChanges = PerforceMonitor.CurrentMaxChanges;
							if (PerforceMonitor.PendingMaxChanges > CurrentMaxChanges)
							{
								PerforceMonitor.PendingMaxChanges = CurrentMaxChanges;
							}
							else
							{
								PerforceMonitor.PendingMaxChanges = CurrentMaxChanges + BuildListExpandCount;
							}
							BuildList.Invalidate();
						}
					}
					else
					{
						ChangesRecord Change = (ChangesRecord)HitTest.Item.Tag;
						if (Workspace.PendingChangeNumber == Change.Number)
						{
							Rectangle SubItemRect = HitTest.Item.SubItems[StatusColumn.Index].Bounds;

							if (Workspace.IsBusy())
							{
								Rectangle CancelRect = new Rectangle(SubItemRect.Right - 16, SubItemRect.Top, 16, SubItemRect.Height);
								Rectangle InfoRect = new Rectangle(SubItemRect.Right - 32, SubItemRect.Top, 16, SubItemRect.Height);
								if (CancelRect.Contains(Args.Location))
								{
									CancelWorkspaceUpdate();
								}
								else if (InfoRect.Contains(Args.Location) && !Splitter.IsLogVisible())
								{
									ToggleLogVisibility();
								}
							}
							else
							{
								Rectangle HappyRect = new Rectangle(SubItemRect.Right - 32, SubItemRect.Top, 16, SubItemRect.Height);
								Rectangle FrownRect = new Rectangle(SubItemRect.Right - 16, SubItemRect.Top, 16, SubItemRect.Height);
								if (HappyRect.Contains(Args.Location))
								{
									EventMonitor.PostEvent(Change.Number, EventType.Good);
									BuildList.Invalidate();
								}
								else if (FrownRect.Contains(Args.Location))
								{
									EventMonitor.PostEvent(Change.Number, EventType.Bad);
									BuildList.Invalidate();
								}
							}
						}
						else
						{
							Rectangle SyncBadgeRectangle = GetSyncBadgeRectangle(HitTest.Item.SubItems[StatusColumn.Index].Bounds);
							if (SyncBadgeRectangle.Contains(Args.Location) && CanSyncChange(Change.Number))
							{
								StartSync(Change.Number, false, null);
							}
						}

						if (DescriptionColumn.Index < HitTest.Item.SubItems.Count && HitTest.Item.SubItems[DescriptionColumn.Index] == HitTest.SubItem)
						{
							ChangeLayoutInfo LayoutInfo = GetChangeLayoutInfo(Change);
							if (LayoutInfo.DescriptionBadges.Count > 0)
							{
								Point BuildListLocation = GetBadgeListLocation(LayoutInfo.DescriptionBadges, HitTest.SubItem.Bounds, HorizontalAlign.Right, VerticalAlignment.Middle);
								BuildListLocation.Offset(-2, 0);

								foreach (BadgeInfo Badge in LayoutInfo.DescriptionBadges)
								{
									Rectangle BadgeBounds = Badge.GetBounds(BuildListLocation);
									if (BadgeBounds.Contains(Args.Location) && Badge.ClickHandler != null)
									{
										Badge.ClickHandler();
										break;
									}
								}
							}
						}

						if (CISColumn.Index < HitTest.Item.SubItems.Count && HitTest.Item.SubItems[CISColumn.Index] == HitTest.SubItem)
						{
							ChangeLayoutInfo LayoutInfo = GetChangeLayoutInfo(Change);
							if (LayoutInfo.BuildBadges.Count > 0)
							{
								Point BuildListLocation = GetBadgeListLocation(LayoutInfo.BuildBadges, HitTest.SubItem.Bounds, HorizontalAlign.Center, VerticalAlignment.Middle);
								BuildListLocation.X = Math.Max(BuildListLocation.X, HitTest.SubItem.Bounds.Left);

								BadgeInfo? BadgeInfo = HitTestBadge(Args.Location, LayoutInfo.BuildBadges, BuildListLocation);
								if (BadgeInfo != null && BadgeInfo.ClickHandler != null)
								{
									BadgeInfo.ClickHandler();
								}
							}
						}

						foreach (ColumnHeader CustomColumn in CustomColumns)
						{
							if (CustomColumn.Index < HitTest.Item.SubItems.Count && HitTest.Item.SubItems[CustomColumn.Index] == HitTest.SubItem)
							{
								ChangeLayoutInfo LayoutInfo = GetChangeLayoutInfo((ChangesRecord)HitTest.Item.Tag);

								List<BadgeInfo>? Badges;
								if (LayoutInfo.CustomBadges.TryGetValue(CustomColumn.Text, out Badges) && Badges.Count > 0)
								{
									Point ListLocation = GetBadgeListLocation(Badges, HitTest.SubItem.Bounds, HorizontalAlign.Center, VerticalAlignment.Middle);

									BadgeInfo? BadgeInfo = HitTestBadge(Args.Location, Badges, ListLocation);
									if (BadgeInfo != null && BadgeInfo.ClickHandler != null)
									{
										BadgeInfo.ClickHandler();
									}
								}
							}
						}
					}
				}
			}
			else if (Args.Button == MouseButtons.Right)
			{
				ListViewHitTestInfo HitTest = BuildList.HitTest(Args.Location);
				if (HitTest.Item != null && HitTest.Item.Tag != null)
				{
					if (BuildList.SelectedItems.Count > 1 && BuildList.SelectedItems.Contains(HitTest.Item))
					{
						bool bIsTimeColumn = (HitTest.Item.SubItems.IndexOf(HitTest.SubItem) == TimeColumn.Index);
						BuildListMultiContextMenu_TimeZoneSeparator.Visible = bIsTimeColumn;
						BuildListMultiContextMenu_ShowLocalTimes.Visible = bIsTimeColumn;
						BuildListMultiContextMenu_ShowLocalTimes.Checked = Settings.bShowLocalTimes;
						BuildListMultiContextMenu_ShowServerTimes.Visible = bIsTimeColumn;
						BuildListMultiContextMenu_ShowServerTimes.Checked = !Settings.bShowLocalTimes;

						BuildListMultiContextMenu.Show(BuildList, Args.Location);
					}
					else
					{
						ContextMenuChange = (ChangesRecord)HitTest.Item.Tag;

						BuildListContextMenu_WithdrawReview.Visible = (EventMonitor.GetReviewByCurrentUser(ContextMenuChange.Number) != null);
						BuildListContextMenu_StartInvestigating.Visible = !EventMonitor.IsUnderInvestigationByCurrentUser(ContextMenuChange.Number);
						BuildListContextMenu_FinishInvestigating.Visible = EventMonitor.IsUnderInvestigation(ContextMenuChange.Number);

						string? CommentText;
						bool bHasExistingComment = EventMonitor.GetCommentByCurrentUser(ContextMenuChange.Number, out CommentText);
						BuildListContextMenu_LeaveComment.Visible = !bHasExistingComment;
						BuildListContextMenu_EditComment.Visible = bHasExistingComment;

						bool bIsBusy = Workspace.IsBusy();
						bool bIsCurrentChange = (ContextMenuChange.Number == Workspace.CurrentChangeNumber);
						BuildListContextMenu_Sync.Visible = !bIsBusy;
						BuildListContextMenu_Sync.Font = new Font(SystemFonts.MenuFont, bIsCurrentChange ? FontStyle.Regular : FontStyle.Bold);
						BuildListContextMenu_SyncContentOnly.Visible = !bIsBusy && ShouldSyncPrecompiledEditor;
						BuildListContextMenu_SyncOnlyThisChange.Visible = !bIsBusy && !bIsCurrentChange && ContextMenuChange.Number > Workspace.CurrentChangeNumber && Workspace.CurrentChangeNumber != -1;
						BuildListContextMenu_Build.Visible = !bIsBusy && bIsCurrentChange && !ShouldSyncPrecompiledEditor;
						BuildListContextMenu_Rebuild.Visible = !bIsBusy && bIsCurrentChange && !ShouldSyncPrecompiledEditor;
						BuildListContextMenu_GenerateProjectFiles.Visible = !bIsBusy && bIsCurrentChange;
						BuildListContextMenu_LaunchEditor.Visible = !bIsBusy && ContextMenuChange.Number == Workspace.CurrentChangeNumber;
						BuildListContextMenu_LaunchEditor.Font = new Font(SystemFonts.MenuFont, FontStyle.Bold);
						BuildListContextMenu_OpenVisualStudio.Visible = !bIsBusy && bIsCurrentChange;
						BuildListContextMenu_Cancel.Visible = bIsBusy;

						BisectState State = WorkspaceState.BisectChanges.FirstOrDefault(x => x.Change == ContextMenuChange.Number)?.State ?? BisectState.Include;
						bool bIsBisectMode = IsBisectModeEnabled();
						BuildListContextMenu_Bisect_Pass.Visible = bIsBisectMode && State != BisectState.Pass;
						BuildListContextMenu_Bisect_Fail.Visible = bIsBisectMode && State != BisectState.Fail;
						BuildListContextMenu_Bisect_Exclude.Visible = bIsBisectMode && State != BisectState.Exclude;
						BuildListContextMenu_Bisect_Include.Visible = bIsBisectMode && State != BisectState.Include;
						BuildListContextMenu_Bisect_Separator.Visible = bIsBisectMode;

						BuildListContextMenu_MarkGood.Visible = !bIsBisectMode;
						BuildListContextMenu_MarkBad.Visible = !bIsBisectMode;
						BuildListContextMenu_WithdrawReview.Visible = !bIsBisectMode;

						EventSummary? Summary = EventMonitor.GetSummaryForChange(ContextMenuChange.Number);
						bool bStarred = (Summary != null && Summary.LastStarReview != null && Summary.LastStarReview.Type == EventType.Starred);
						BuildListContextMenu_AddStar.Visible = !bStarred;
						BuildListContextMenu_RemoveStar.Visible = bStarred;

						bool bIsTimeColumn = (HitTest.Item.SubItems.IndexOf(HitTest.SubItem) == TimeColumn.Index);
						BuildListContextMenu_TimeZoneSeparator.Visible = bIsTimeColumn;
						BuildListContextMenu_ShowLocalTimes.Visible = bIsTimeColumn;
						BuildListContextMenu_ShowLocalTimes.Checked = Settings.bShowLocalTimes;
						BuildListContextMenu_ShowServerTimes.Visible = bIsTimeColumn;
						BuildListContextMenu_ShowServerTimes.Checked = !Settings.bShowLocalTimes;

						int CustomToolStart = BuildListContextMenu.Items.IndexOf(BuildListContextMenu_CustomTool_Start) + 1;
						int CustomToolEnd = BuildListContextMenu.Items.IndexOf(BuildListContextMenu_CustomTool_End);
						while (CustomToolEnd > CustomToolStart)
						{
							BuildListContextMenu.Items.RemoveAt(CustomToolEnd - 1);
							CustomToolEnd--;
						}

						ConfigFile? ProjectConfigFile = PerforceMonitor.LatestProjectConfigFile;
						if (ProjectConfigFile != null)
						{
							Dictionary<string, string> Variables = Workspace.GetVariables(GetEditorBuildConfig(), ContextMenuChange.Number, -1);

							string[] ChangeContextMenuEntries = ProjectConfigFile.GetValues("Options.ContextMenu", new string[0]);
							foreach (string ChangeContextMenuEntry in ChangeContextMenuEntries)
							{
								ConfigObject Object = new ConfigObject(ChangeContextMenuEntry);

								string? Label = Object.GetValue("Label");
								string? Execute = Object.GetValue("Execute");
								string? Arguments = Object.GetValue("Arguments");

								if (Label != null && Execute != null)
								{
									Label = Utility.ExpandVariables(Label, Variables);
									Execute = Utility.ExpandVariables(Execute, Variables);
									Arguments = Utility.ExpandVariables(Arguments ?? "", Variables);

									ToolStripMenuItem Item = new ToolStripMenuItem(Label, null, new EventHandler((o, a) => SafeProcessStart(Execute, Arguments)));

									BuildListContextMenu.Items.Insert(CustomToolEnd, Item);
									CustomToolEnd++;
								}
							}
						}

						BuildListContextMenu_CustomTool_End.Visible = (CustomToolEnd > CustomToolStart);

						string? SwarmURL;
						BuildListContextMenu_ViewInSwarm.Visible = ProjectConfigFile != null && TryGetProjectSetting(ProjectConfigFile, "SwarmURL", out SwarmURL);
							
						BuildListContextMenu.Show(BuildList, Args.Location);
					}
				}
			}
		}

		private void BuildList_MouseMove(object sender, MouseEventArgs e)
		{
			ListViewHitTestInfo HitTest = BuildList.HitTest(e.Location);

			bool bNewMouseOverExpandLink = HitTest.Item != null && HitTestExpandLink(e.Location);
			if (bMouseOverExpandLink != bNewMouseOverExpandLink)
			{
				bMouseOverExpandLink = bNewMouseOverExpandLink;
				Cursor = bMouseOverExpandLink ? NativeCursors.Hand : Cursors.Arrow;
				BuildList.Invalidate();
			}

			string? NewHoverBadgeUniqueId = null;
			if (HitTest.Item != null && HitTest.Item.Tag is ChangesRecord)
			{
				int ColumnIndex = HitTest.Item.SubItems.IndexOf(HitTest.SubItem);
				if (ColumnIndex == DescriptionColumn.Index)
				{
					ChangeLayoutInfo LayoutInfo = GetChangeLayoutInfo((ChangesRecord)HitTest.Item.Tag);
					if (LayoutInfo.DescriptionBadges.Count > 0)
					{
						Point ListLocation = GetBadgeListLocation(LayoutInfo.DescriptionBadges, HitTest.SubItem.Bounds, HorizontalAlign.Right, VerticalAlignment.Middle);
						NewHoverBadgeUniqueId = HitTestBadge(e.Location, LayoutInfo.DescriptionBadges, ListLocation)?.UniqueId;
					}
				}
				else if (ColumnIndex == CISColumn.Index)
				{
					ChangeLayoutInfo LayoutInfo = GetChangeLayoutInfo((ChangesRecord)HitTest.Item.Tag);
					if (LayoutInfo.BuildBadges.Count > 0)
					{
						Point BuildListLocation = GetBadgeListLocation(LayoutInfo.BuildBadges, HitTest.SubItem.Bounds, HorizontalAlign.Center, VerticalAlignment.Middle);
						BuildListLocation.X = Math.Max(BuildListLocation.X, HitTest.SubItem.Bounds.Left);

						BadgeInfo? Badge = HitTestBadge(e.Location, LayoutInfo.BuildBadges, BuildListLocation);
						NewHoverBadgeUniqueId = (Badge != null) ? Badge.UniqueId : null;

						if (HoverBadgeUniqueId != NewHoverBadgeUniqueId)
						{
							if (Badge != null && Badge.ToolTip != null && Badge.BackgroundColor.A != 0)
							{
								BuildListToolTip.Show(Badge.ToolTip, BuildList, new Point(BuildListLocation.X + Badge.Offset, HitTest.Item.Bounds.Bottom + 2));
							}
							else
							{
								BuildListToolTip.Hide(BuildList);
							}
						}
					}
				}
				else if (CustomColumns.Contains(BuildList.Columns[ColumnIndex]))
				{
					ColumnHeader Column = BuildList.Columns[ColumnIndex];

					ChangeLayoutInfo LayoutInfo = GetChangeLayoutInfo((ChangesRecord)HitTest.Item.Tag);

					List<BadgeInfo>? Badges;
					if (LayoutInfo.CustomBadges.TryGetValue(Column.Text, out Badges) && Badges.Count > 0)
					{
						Point ListLocation = GetBadgeListLocation(Badges, HitTest.SubItem.Bounds, HorizontalAlign.Center, VerticalAlignment.Middle);
						NewHoverBadgeUniqueId = HitTestBadge(e.Location, Badges, ListLocation)?.UniqueId;
					}
				}
			}
			if (HoverBadgeUniqueId != NewHoverBadgeUniqueId)
			{
				HoverBadgeUniqueId = NewHoverBadgeUniqueId;
				BuildList.Invalidate();
			}

			bool bNewHoverSync = false;
			if (HitTest.Item != null && StatusColumn.Index < HitTest.Item.SubItems.Count)
			{
				bNewHoverSync = GetSyncBadgeRectangle(HitTest.Item.SubItems[StatusColumn.Index].Bounds).Contains(e.Location);
			}
			if (bNewHoverSync != bHoverSync)
			{
				bHoverSync = bNewHoverSync;
				BuildList.Invalidate();
			}
		}

		private BadgeInfo? HitTestBadge(Point Location, List<BadgeInfo> BadgeList, Point ListLocation)
		{
			foreach (BadgeInfo Badge in BadgeList)
			{
				Rectangle BadgeBounds = Badge.GetBounds(ListLocation);
				if (BadgeBounds.Contains(Location))
				{
					return Badge;
				}
			}
			return null;
		}

		private List<IArchiveInfo> GetSelectedArchives(IReadOnlyList<IArchiveInfo> Archives)
		{
			Dictionary<string, KeyValuePair<IArchiveInfo, int>> ArchiveTypeToSelection = new Dictionary<string, KeyValuePair<IArchiveInfo, int>>();
			foreach (IArchiveInfo Archive in Archives)
			{
				ArchiveSettings ArchiveSettings = Settings.Archives.FirstOrDefault(x => x.Type == Archive.Type);
				if (ArchiveSettings != null && ArchiveSettings.bEnabled)
				{
					int Preference = ArchiveSettings.Order.IndexOf(Archive.Name);
					if (Preference == -1)
					{
						Preference = ArchiveSettings.Order.Count;
					}

					KeyValuePair<IArchiveInfo, int> ExistingItem;
					if (!ArchiveTypeToSelection.TryGetValue(Archive.Type, out ExistingItem) || ExistingItem.Value > Preference)
					{
						ArchiveTypeToSelection[Archive.Type] = new KeyValuePair<IArchiveInfo, int>(Archive, Preference);
					}
				}
			}
			return ArchiveTypeToSelection.Select(x => x.Value.Key).ToList();
		}

		private void ClearSelectedArchives()
		{
			foreach (ArchiveSettings ArchiveSettings in Settings.Archives)
			{
				ArchiveSettings.bEnabled = false;
			}
			Settings.Save();

			UpdateSelectedArchives();
		}

		private void SetSelectedArchive(IArchiveInfo Archive, bool bSelected)
		{
			ArchiveSettings ArchiveSettings = Settings.Archives.FirstOrDefault(x => x.Type == Archive.Type);
			if (ArchiveSettings == null)
			{
				ArchiveSettings = new ArchiveSettings(bSelected, Archive.Type, new string[] { Archive.Name });
				Settings.Archives.Add(ArchiveSettings);
			}
			else if (bSelected)
			{
				ArchiveSettings.bEnabled = true;
				ArchiveSettings.Order.Remove(Archive.Name);
				ArchiveSettings.Order.Insert(0, Archive.Name);
			}
			else
			{
				ArchiveSettings.bEnabled = false;
			}
			Settings.Save();

			UpdateSelectedArchives();
		}

		private void UpdateSelectedArchives()
		{
			UpdateBuildSteps();
			UpdateSyncActionCheckboxes();

			BuildList.Invalidate();
		}

		private void OptionsButton_Click(object sender, EventArgs e)
		{
			OptionsContextMenu_AutoResolveConflicts.Checked = Settings.bAutoResolveConflicts;

			OptionsContextMenu_SyncPrecompiledBinaries.DropDownItems.Clear();

			IReadOnlyList<IArchiveInfo> Archives = GetArchives();
			if (Archives == null || Archives.Count == 0)
			{
				OptionsContextMenu_SyncPrecompiledBinaries.Enabled = false;
				OptionsContextMenu_SyncPrecompiledBinaries.ToolTipText = String.Format("Precompiled binaries are not available for {0}", SelectedProjectIdentifier);
				OptionsContextMenu_SyncPrecompiledBinaries.Checked = false;
			}
			else
			{
				List<IArchiveInfo> SelectedArchives = GetSelectedArchives(Archives);

				OptionsContextMenu_SyncPrecompiledBinaries.Enabled = true;
				OptionsContextMenu_SyncPrecompiledBinaries.ToolTipText = null;
				OptionsContextMenu_SyncPrecompiledBinaries.Checked = SelectedArchives.Count > 0;

				if (Archives.Count > 1 || Archives[0].Type != IArchiveInfo.EditorArchiveType)
				{
					ToolStripMenuItem DisableItem = new ToolStripMenuItem("Disable (compile locally)");
					DisableItem.Checked = (SelectedArchives.Count == 0);
					DisableItem.Click += (Sender, Args) => ClearSelectedArchives();
					OptionsContextMenu_SyncPrecompiledBinaries.DropDownItems.Add(DisableItem);

					ToolStripSeparator Separator = new ToolStripSeparator();
					OptionsContextMenu_SyncPrecompiledBinaries.DropDownItems.Add(Separator);

					foreach (IArchiveInfo Archive in Archives)
					{
						ToolStripMenuItem Item = new ToolStripMenuItem(Archive.Name);
						Item.Enabled = Archive.Exists();
						if (!Item.Enabled)
						{
							Item.ToolTipText = String.Format("No valid archives found at {0}", Archive.BasePath);
						}
						Item.Checked = SelectedArchives.Contains(Archive);
						Item.Click += (Sender, Args) => SetSelectedArchive(Archive, !Item.Checked);
						OptionsContextMenu_SyncPrecompiledBinaries.DropDownItems.Add(Item);
					}
				}
			}

			OptionsContextMenu_EditorBuildConfiguration.Enabled = !ShouldSyncPrecompiledEditor;
			UpdateCheckedBuildConfig();
			OptionsContextMenu_CustomizeBuildSteps.Enabled = (Workspace != null);
			OptionsContextMenu_EditorArguments.Checked = Settings.EditorArguments.Any(x => x.Item2);
			OptionsContextMenu_ScheduledSync.Checked = Settings.bScheduleEnabled;
			OptionsContextMenu_TimeZone_Local.Checked = Settings.bShowLocalTimes;
			OptionsContextMenu_TimeZone_PerforceServer.Checked = !Settings.bShowLocalTimes;
			OptionsContextMenu_ShowChanges_ShowUnreviewed.Checked = Settings.bShowUnreviewedChanges;
			OptionsContextMenu_ShowChanges_ShowAutomated.Checked = Settings.bShowAutomatedChanges;
			OptionsContextMenu_TabNames_Stream.Checked = Settings.TabLabels == TabLabels.Stream;
			OptionsContextMenu_TabNames_WorkspaceName.Checked = Settings.TabLabels == TabLabels.WorkspaceName;
			OptionsContextMenu_TabNames_WorkspaceRoot.Checked = Settings.TabLabels == TabLabels.WorkspaceRoot;
			OptionsContextMenu_TabNames_ProjectFile.Checked = Settings.TabLabels == TabLabels.ProjectFile;
			OptionsContextMenu.Show(OptionsButton, new Point(OptionsButton.Width - OptionsContextMenu.Size.Width, OptionsButton.Height));
		}

		private IReadOnlyList<IArchiveInfo> GetArchives()
		{
			IReadOnlyList<IArchiveInfo>? AvailableArchives = JupiterMonitor?.AvailableArchives;
			if (AvailableArchives != null && AvailableArchives.Count != 0)
				return AvailableArchives;

			// if jupiter had no archives we fallback to the perforce monitor
			if (PerforceMonitor != null)
			{
				return PerforceMonitor.AvailableArchives;
			}

			return new List<IArchiveInfo>();
		}

		private void BuildAfterSyncCheckBox_CheckedChanged(object sender, EventArgs e)
		{
			Settings.bBuildAfterSync = BuildAfterSyncCheckBox.Checked;
			Settings.Save();

			UpdateSyncActionCheckboxes();
		}

		private void RunAfterSyncCheckBox_CheckedChanged(object sender, EventArgs e)
		{
			Settings.bRunAfterSync = RunAfterSyncCheckBox.Checked;
			Settings.Save();

			UpdateSyncActionCheckboxes();
		}

		private void OpenSolutionAfterSyncCheckBox_CheckedChanged(object sender, EventArgs e)
		{
			Settings.bOpenSolutionAfterSync = OpenSolutionAfterSyncCheckBox.Checked;
			Settings.Save();

			UpdateSyncActionCheckboxes();
		}

		private void UpdateSyncActionCheckboxes()
		{
			BuildAfterSyncCheckBox.Checked = Settings.bBuildAfterSync;
			RunAfterSyncCheckBox.Checked = Settings.bRunAfterSync;
			OpenSolutionAfterSyncCheckBox.Checked = Settings.bOpenSolutionAfterSync;

			if (Workspace == null || Workspace.IsBusy())
			{
				AfterSyncingLabel.Enabled = false;
				BuildAfterSyncCheckBox.Enabled = false;
				RunAfterSyncCheckBox.Enabled = false;
				OpenSolutionAfterSyncCheckBox.Enabled = false;
			}
			else
			{
				AfterSyncingLabel.Enabled = true;
				BuildAfterSyncCheckBox.Enabled = bHasBuildSteps;
				RunAfterSyncCheckBox.Enabled = BuildAfterSyncCheckBox.Checked || ShouldSyncPrecompiledEditor;
				OpenSolutionAfterSyncCheckBox.Enabled = !ShouldSyncPrecompiledEditor;
			}
		}

		private void BuildList_FontChanged(object? sender, EventArgs e)
		{
			if (BuildFont != null)
			{
				BuildFont.Dispose();
			}
			BuildFont = BuildList.Font;

			if (SelectedBuildFont != null)
			{
				SelectedBuildFont.Dispose();
			}
			SelectedBuildFont = new Font(BuildFont, FontStyle.Bold);

			if (BadgeFont != null)
			{
				BadgeFont.Dispose();
			}
			BadgeFont = new Font(BuildFont.FontFamily, BuildFont.SizeInPoints - 2, FontStyle.Bold);
		}

		public void ExecCommand(string Description, string StatusText, string FileName, string Arguments, string WorkingDir, bool bUseLogWindow)
		{
			if (Workspace != null && !Workspace.IsBusy())
			{
				Guid Id = Guid.NewGuid();

				BuildStep CustomBuildStep = new BuildStep(Guid.NewGuid(), 0, Description, StatusText, 1, FileName, Arguments, WorkingDir, bUseLogWindow);

				List<ConfigObject> UserBuildSteps = new List<ConfigObject>();
				UserBuildSteps.Add(CustomBuildStep.ToConfigObject());

				WorkspaceUpdateContext Context = new WorkspaceUpdateContext(Workspace.CurrentChangeNumber, WorkspaceUpdateOptions.Build, GetEditorBuildConfig(), null, UserBuildSteps, new HashSet<Guid> { CustomBuildStep.UniqueId });
				StartWorkspaceUpdate(Context, null);
			}
		}

		public void SyncChange(int ChangeNumber, bool bSyncOnly, WorkspaceUpdateCallback? Callback)
		{
			if(Workspace != null)
			{
				Owner.ShowAndActivate();
				SelectChange(ChangeNumber);
				StartSync(ChangeNumber, bSyncOnly, Callback);
			}
		}

		public void SyncLatestChange()
		{
			SyncLatestChange(null);
		}

		public void SyncLatestChange(WorkspaceUpdateCallback? Callback)
		{
			if (Workspace != null)
			{
				int ChangeNumber;
				if (!FindChangeToSync(Settings.SyncTypeID, out ChangeNumber))
				{
					string SyncTypeName = "";
					foreach (LatestChangeType ChangeType in GetCustomLatestChangeTypes())
					{
						if (ChangeType.Name == Settings.SyncTypeID)
						{
							SyncTypeName = ChangeType.Name;
							break;
						}
					}
					ShowErrorDialog(String.Format("Couldn't find any {0}changelist. Double-click on the change you want to sync manually.", SyncTypeName));
				}
				else if (ChangeNumber < Workspace.CurrentChangeNumber)
				{
					ShowErrorDialog("Workspace is already synced to a later change ({0} vs {1}).", Workspace.CurrentChangeNumber, ChangeNumber);
				}
				else if (ChangeNumber >= PerforceMonitor.LastChangeByCurrentUser || MessageBox.Show(String.Format("The changelist that would be synced is before the last change you submitted.\n\nIf you continue, your changes submitted after CL {0} will be locally removed from your workspace until you can sync past them again.\n\nAre you sure you want to continue?", ChangeNumber), "Local changes will be removed", MessageBoxButtons.YesNo) == DialogResult.Yes)
				{
					SyncChange(ChangeNumber, false, Callback);
				}
			}
		}

		private void BuildListContextMenu_MarkGood_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				EventMonitor.PostEvent(ContextMenuChange.Number, EventType.Good);
			}
		}

		private void BuildListContextMenu_MarkBad_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				EventMonitor.PostEvent(ContextMenuChange.Number, EventType.Bad);
			}
		}

		private void BuildListContextMenu_WithdrawReview_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				EventMonitor.PostEvent(ContextMenuChange.Number, EventType.Unknown);
			}
		}

		private void BuildListContextMenu_Sync_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				StartSync(ContextMenuChange.Number);
			}
		}

		private void BuildListContextMenu_SyncContentOnly_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				StartWorkspaceUpdate(ContextMenuChange.Number, WorkspaceUpdateOptions.Sync | WorkspaceUpdateOptions.ContentOnly);
			}
		}

		private void BuildListContextMenu_SyncOnlyThisChange_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				StartWorkspaceUpdate(ContextMenuChange.Number, WorkspaceUpdateOptions.SyncSingleChange);
			}
		}

		private void BuildListContextMenu_Build_Click(object sender, EventArgs e)
		{
			StartWorkspaceUpdate(Workspace.CurrentChangeNumber, WorkspaceUpdateOptions.SyncArchives | WorkspaceUpdateOptions.Build);
		}

		private void BuildListContextMenu_Rebuild_Click(object sender, EventArgs e)
		{
			StartWorkspaceUpdate(Workspace.CurrentChangeNumber, WorkspaceUpdateOptions.SyncArchives | WorkspaceUpdateOptions.Build | WorkspaceUpdateOptions.Clean);
		}

		private void BuildListContextMenu_GenerateProjectFiles_Click(object sender, EventArgs e)
		{
			StartWorkspaceUpdate(Workspace.CurrentChangeNumber, WorkspaceUpdateOptions.GenerateProjectFiles);
		}

		private void BuildListContextMenu_CancelSync_Click(object sender, EventArgs e)
		{
			CancelWorkspaceUpdate();
		}

		private void BuildListContextMenu_MoreInfo_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				Program.SpawnP4VC(String.Format("{0} change {1}", PerforceSettings.GetArgumentsForExternalProgram(true), ContextMenuChange.Number));
			}
		}

		private void BuildListContextMenu_ViewInSwarm_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				string? SwarmURL;
				if (TryGetProjectSetting(PerforceMonitor.LatestProjectConfigFile, "SwarmURL", out SwarmURL))
				{
					Utility.OpenUrl(String.Format("{0}/changes/{1}", SwarmURL, ContextMenuChange.Number));
				}
				else
				{
					MessageBox.Show("Swarm URL is not configured.");
				}
			}
		}

		private void BuildListContextMenu_CopyChangelistNumber_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				System.Windows.Forms.Clipboard.SetText(ContextMenuChange.Number.ToString());
			}
		}

		private void BuildListContextMenu_AddStar_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				if (MessageBox.Show("Starred builds are meant to convey a stable, verified build to the rest of the team. Do not star a build unless it has been fully tested.\n\nAre you sure you want to star this build?", "Confirm star", MessageBoxButtons.YesNo) == DialogResult.Yes)
				{
					EventMonitor.PostEvent(ContextMenuChange.Number, EventType.Starred);
				}
			}
		}

		private void BuildListContextMenu_RemoveStar_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				EventSummary? Summary = EventMonitor.GetSummaryForChange(ContextMenuChange.Number);
				if (Summary != null && Summary.LastStarReview != null && Summary.LastStarReview.Type == EventType.Starred)
				{
					string Message = String.Format("This change was starred by {0}. Are you sure you want to remove it?", FormatUserName(Summary.LastStarReview.UserName));
					if (MessageBox.Show(Message, "Confirm removing star", MessageBoxButtons.YesNo) == DialogResult.Yes)
					{
						EventMonitor.PostEvent(ContextMenuChange.Number, EventType.Unstarred);
					}
				}
			}
		}

		private void BuildListContextMenu_LaunchEditor_Click(object sender, EventArgs e)
		{
			LaunchEditor();
		}

		private void BuildListContextMenu_StartInvestigating_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				string Message = String.Format("All changes from {0} onwards will be marked as bad while you are investigating an issue.\n\nAre you sure you want to continue?", ContextMenuChange.Number);
				if (MessageBox.Show(Message, "Confirm investigating", MessageBoxButtons.YesNo) == DialogResult.Yes)
				{
					EventMonitor.StartInvestigating(ContextMenuChange.Number);
				}
			}
		}

		private void BuildListContextMenu_FinishInvestigating_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				int StartChangeNumber = EventMonitor.GetInvestigationStartChangeNumber(ContextMenuChange.Number);
				if (StartChangeNumber != -1)
				{
					if (ContextMenuChange.Number > StartChangeNumber)
					{
						string Message = String.Format("Mark all changes between {0} and {1} as bad?", StartChangeNumber, ContextMenuChange.Number);
						if (MessageBox.Show(Message, "Finish investigating", MessageBoxButtons.YesNo) == DialogResult.Yes)
						{
							foreach (ChangesRecord Change in PerforceMonitor.GetChanges())
							{
								if (Change.Number >= StartChangeNumber && Change.Number < ContextMenuChange.Number)
								{
									EventMonitor.PostEvent(Change.Number, EventType.Bad);
								}
							}
						}
					}
					EventMonitor.FinishInvestigating(ContextMenuChange.Number);
				}
			}
		}

		private void OnlyShowReviewedCheckBox_CheckedChanged(object sender, EventArgs e)
		{
			UpdateBuildList();
			ShrinkNumRequestedBuilds();
		}

		public void Activate()
		{
			UpdateSyncActionCheckboxes();

			if (PerforceMonitor != null)
			{
				PerforceMonitor.Refresh();
			}

			if (DesiredTaskbarState.Item1 == TaskbarState.Error)
			{
				DesiredTaskbarState = Tuple.Create(TaskbarState.NoProgress, 0.0f);
				Owner.UpdateProgress();
			}
		}

		public void Deactivate()
		{
			ShrinkNumRequestedBuilds();
		}

		private void BuildList_ItemMouseHover(object sender, ListViewItemMouseHoverEventArgs Args)
		{
			Point ClientPoint = BuildList.PointToClient(Cursor.Position);
			if (Args.Item.SubItems.Count >= 6)
			{
				if (Args.Item.Bounds.Contains(ClientPoint))
				{
					ChangesRecord Change = (ChangesRecord)Args.Item.Tag;
					if (Change == null)
					{
						return;
					}

					Rectangle CISBounds = Args.Item.SubItems[CISColumn.Index].Bounds;
					if (CISBounds.Contains(ClientPoint))
					{
						return;
					}

					Rectangle DescriptionBounds = Args.Item.SubItems[DescriptionColumn.Index].Bounds;
					if (DescriptionBounds.Contains(ClientPoint))
					{
						ChangeLayoutInfo LayoutInfo = GetChangeLayoutInfo(Change);
						if (LayoutInfo.DescriptionBadges.Count == 0 || ClientPoint.X < GetBadgeListLocation(LayoutInfo.DescriptionBadges, DescriptionBounds, HorizontalAlign.Right, VerticalAlignment.Middle).X - 2)
						{
							BuildListToolTip.Show(Change.Description, BuildList, new Point(DescriptionBounds.Left, DescriptionBounds.Bottom + 2));
							return;
						}
					}

					EventSummary? Summary = EventMonitor.GetSummaryForChange(Change.Number);
					if (Summary != null)
					{
						StringBuilder SummaryText = new StringBuilder();
						if (Summary.Comments.Count > 0)
						{
							foreach (CommentData Comment in Summary.Comments)
							{
								if (!String.IsNullOrWhiteSpace(Comment.Text))
								{
									SummaryText.AppendFormat("{0}: \"{1}\"\n", FormatUserName(Comment.UserName), Comment.Text);
								}
							}
							if (SummaryText.Length > 0)
							{
								SummaryText.Append("\n");
							}
						}
						AppendUserList(SummaryText, "\n", "Compiled by {0}.", Summary.Reviews.Where(x => x.Type == EventType.Compiles));
						AppendUserList(SummaryText, "\n", "Failed to compile for {0}.", Summary.Reviews.Where(x => x.Type == EventType.DoesNotCompile));
						AppendUserList(SummaryText, "\n", "Marked good by {0}.", Summary.Reviews.Where(x => x.Type == EventType.Good));
						AppendUserList(SummaryText, "\n", "Marked bad by {0}.", Summary.Reviews.Where(x => x.Type == EventType.Bad));
						if (Summary.LastStarReview != null)
						{
							AppendUserList(SummaryText, "\n", "Starred by {0}.", new EventData[] { Summary.LastStarReview });
						}
						if (SummaryText.Length > 0)
						{
							Rectangle SummaryBounds = Args.Item.SubItems[StatusColumn.Index].Bounds;
							BuildListToolTip.Show(SummaryText.ToString(), BuildList, new Point(SummaryBounds.Left, SummaryBounds.Bottom));
							return;
						}
					}
				}
			}

			BuildListToolTip.Hide(BuildList);
		}

		private void BuildList_MouseLeave(object sender, EventArgs e)
		{
			BuildListToolTip.Hide(BuildList);

			if (HoverBadgeUniqueId != null)
			{
				HoverBadgeUniqueId = null;
				BuildList.Invalidate();
			}

			if (bMouseOverExpandLink)
			{
				Cursor = Cursors.Arrow;
				bMouseOverExpandLink = false;
				BuildList.Invalidate();
			}
		}

		private void OptionsContextMenu_ShowLog_Click(object sender, EventArgs e)
		{
			ToggleLogVisibility();
		}

		private void ToggleLogVisibility()
		{
			Splitter.SetLogVisibility(!Splitter.IsLogVisible());
		}

		private void Splitter_OnVisibilityChanged(bool bVisible)
		{
			Settings.bShowLogWindow = bVisible;
			Settings.Save();

			UpdateStatusPanel();
		}

		private void OptionsContextMenu_AutoResolveConflicts_Click(object sender, EventArgs e)
		{
			OptionsContextMenu_AutoResolveConflicts.Checked ^= true;
			Settings.bAutoResolveConflicts = OptionsContextMenu_AutoResolveConflicts.Checked;
			Settings.Save();
		}

		private void OptionsContextMenu_EditorArguments_Click(object sender, EventArgs e)
		{
			ModifyEditorArguments();
		}

		private bool ModifyEditorArguments()
		{
			ArgumentsWindow Arguments = new ArgumentsWindow(Settings.EditorArguments, Settings.bEditorArgumentsPrompt);
			if (Arguments.ShowDialog(this) == DialogResult.OK)
			{
				Settings.EditorArguments = Arguments.GetItems();
				Settings.bEditorArgumentsPrompt = Arguments.PromptBeforeLaunch;
				Settings.Save();
				return true;
			}
			return false;
		}

		private void BuildListContextMenu_OpenVisualStudio_Click(object sender, EventArgs e)
		{
			OpenSolution();
		}

		private void OpenSolution()
		{
			string MasterProjectName = "UE4";
			if (FileReference.Exists(FileReference.Combine(BranchDirectoryName, "UE5.sln")))
			{
				MasterProjectName = "UE5";
			}

			FileReference MasterProjectNameFileName = FileReference.Combine(BranchDirectoryName, "Engine", "Intermediate", "ProjectFiles", "MasterProjectName.txt");
			if (FileReference.Exists(MasterProjectNameFileName))
			{
				try
				{
					MasterProjectName = FileReference.ReadAllText(MasterProjectNameFileName).Trim();
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Unable to read '{File}'", MasterProjectNameFileName);
				}
			}

			FileReference SolutionFileName = FileReference.Combine(BranchDirectoryName, MasterProjectName + ".sln");
			if (!FileReference.Exists(SolutionFileName))
			{
				MessageBox.Show(String.Format("Couldn't find solution at {0}", SolutionFileName));
			}
			else
			{
				ProcessStartInfo StartInfo = new ProcessStartInfo(SolutionFileName.FullName);
				StartInfo.UseShellExecute = true;
				StartInfo.WorkingDirectory = BranchDirectoryName.FullName;
				SafeProcessStart(StartInfo);
			}
		}

		private void OptionsContextMenu_BuildConfig_Debug_Click(object sender, EventArgs e)
		{
			UpdateBuildConfig(BuildConfig.Debug);
		}

		private void OptionsContextMenu_BuildConfig_DebugGame_Click(object sender, EventArgs e)
		{
			UpdateBuildConfig(BuildConfig.DebugGame);
		}

		private void OptionsContextMenu_BuildConfig_Development_Click(object sender, EventArgs e)
		{
			UpdateBuildConfig(BuildConfig.Development);
		}

		void UpdateBuildConfig(BuildConfig NewConfig)
		{
			Settings.CompiledEditorBuildConfig = NewConfig;
			Settings.Save();
			UpdateCheckedBuildConfig();
		}

		void UpdateCheckedBuildConfig()
		{
			BuildConfig EditorBuildConfig = GetEditorBuildConfig();

			OptionsContextMenu_BuildConfig_Debug.Checked = (EditorBuildConfig == BuildConfig.Debug);
			OptionsContextMenu_BuildConfig_Debug.Enabled = (!ShouldSyncPrecompiledEditor || EditorBuildConfig == BuildConfig.Debug);

			OptionsContextMenu_BuildConfig_DebugGame.Checked = (EditorBuildConfig == BuildConfig.DebugGame);
			OptionsContextMenu_BuildConfig_DebugGame.Enabled = (!ShouldSyncPrecompiledEditor || EditorBuildConfig == BuildConfig.DebugGame);

			OptionsContextMenu_BuildConfig_Development.Checked = (EditorBuildConfig == BuildConfig.Development);
			OptionsContextMenu_BuildConfig_Development.Enabled = (!ShouldSyncPrecompiledEditor || EditorBuildConfig == BuildConfig.Development);
		}

		private void OptionsContextMenu_ScheduleSync_Click(object sender, EventArgs e)
		{
			Owner.SetupScheduledSync();
		}

		public void ScheduleTimerElapsed()
		{
			if (Settings.bScheduleEnabled)
			{
				// Check the project config if they have a scheduled setting selected.
				string? ScheduledSyncTypeID = null;
				UserSelectedProjectSettings ProjectSetting = Settings.ScheduleProjects.FirstOrDefault(x => x.LocalPath != null && x.LocalPath.Equals(SelectedProject.LocalPath, StringComparison.OrdinalIgnoreCase));
				if (ProjectSetting != null)
				{
					ScheduledSyncTypeID = ProjectSetting.ScheduledSyncTypeID;
				}

				Logger.LogInformation("Scheduled sync at {0} for {1}.",
					DateTime.Now,
					ScheduledSyncTypeID != null ? ScheduledSyncTypeID : "Default");

				int ChangeNumber;
				if (!FindChangeToSync(ScheduledSyncTypeID, out ChangeNumber))
				{
					SyncLog.AppendLine("Couldn't find any matching change");
				}
				else if (Workspace.CurrentChangeNumber >= ChangeNumber)
				{
					Logger.LogInformation("Sync ignored; already at or ahead of CL ({CurrentChange} >= {Change})", Workspace.CurrentChangeNumber, ChangeNumber);
				}
				else
				{
					WorkspaceUpdateOptions Options = WorkspaceUpdateOptions.Sync | WorkspaceUpdateOptions.SyncArchives | WorkspaceUpdateOptions.GenerateProjectFiles | WorkspaceUpdateOptions.Build | WorkspaceUpdateOptions.ScheduledBuild;
					if (Settings.bAutoResolveConflicts)
					{
						Options |= WorkspaceUpdateOptions.AutoResolveChanges;
					}
					StartWorkspaceUpdate(ChangeNumber, Options);
				}
			}
		}

		bool FindChangeToSync(string? ChangeTypeID, out int ChangeNumber)
		{
			if (ChangeTypeID != null)
			{
				LatestChangeType? ChangeTypeToSyncTo = null;
				foreach (LatestChangeType ChangeType in GetCustomLatestChangeTypes())
				{
					if (ChangeType.Name == ChangeTypeID)
					{
						ChangeTypeToSyncTo = ChangeType;
						break;
					}
				}

				if (ChangeTypeToSyncTo != null)
				{
					return FindChangeToSync(ChangeTypeToSyncTo, out ChangeNumber);
				}
			}

			SyncLog.AppendLine("Invalid or not set Scheduled Sync type using (Sync to Latest Good)");

			return FindChangeToSync(LatestChangeType.LatestGoodChange(), out ChangeNumber);
		}

		bool FindChangeToSync(LatestChangeType ChangeType, out int ChangeNumber)
		{
			for (int Idx = 0; Idx < BuildList.Items.Count; Idx++)
			{
				ChangesRecord Change = (ChangesRecord)BuildList.Items[Idx].Tag;
				if (Change != null)
				{
					EventSummary? Summary = EventMonitor.GetSummaryForChange(Change.Number);
					if (CanSyncChange(Change.Number)
						&& CanSyncChangeType(ChangeType, Change, Summary))
					{
						if (ChangeType.bFindNewestGoodContent)
						{
							ChangeNumber = FindNewestGoodContentChange(Change.Number);
						}
						else
						{
							ChangeNumber = Change.Number;
						}
						
						return true;
					}
				}
			}

			ChangeNumber = -1;
			return false;
		}

		int FindNewestGoodContentChange(int ChangeNumber)
		{
			int Index = SortedChangeNumbers.BinarySearch(ChangeNumber);
			if (Index <= 0)
			{
				return ChangeNumber;
			}

			for (int NextIndex = Index + 1; NextIndex < SortedChangeNumbers.Count; NextIndex++)
			{
				int NextChangeNumber = SortedChangeNumbers[NextIndex];

				PerforceChangeDetails? Details;
				if (!PerforceMonitor.TryGetChangeDetails(NextChangeNumber, out Details) || Details.bContainsCode)
				{
					break;
				}

				EventSummary? Summary = EventMonitor.GetSummaryForChange(NextChangeNumber);
				if (Summary != null && Summary.Verdict == ReviewVerdict.Bad)
				{
					break;
				}

				Index = NextIndex;
			}

			return SortedChangeNumbers[Index];
		}

		private void BuildListContextMenu_LeaveOrEditComment_Click(object sender, EventArgs e)
		{
			if (ContextMenuChange != null)
			{
				LeaveCommentWindow LeaveComment = new LeaveCommentWindow();

				string? CommentText;
				if (EventMonitor.GetCommentByCurrentUser(ContextMenuChange.Number, out CommentText))
				{
					LeaveComment.CommentTextBox.Text = CommentText;
				}

				if (LeaveComment.ShowDialog() == System.Windows.Forms.DialogResult.OK)
				{
					EventMonitor.PostComment(ContextMenuChange.Number, LeaveComment.CommentTextBox.Text);
				}
			}
		}

		private void BuildListContextMenu_ShowServerTimes_Click(object sender, EventArgs e)
		{
			Settings.bShowLocalTimes = false;
			Settings.Save();
			BuildList.Items.Clear(); // Need to clear list to rebuild groups, populate time column again
			UpdateBuildList();
		}

		private void BuildListContextMenu_ShowLocalTimes_Click(object sender, EventArgs e)
		{
			Settings.bShowLocalTimes = true;
			Settings.Save();
			BuildList.Items.Clear(); // Need to clear list to rebuild groups, populate time column again
			UpdateBuildList();
		}

		private void MoreToolsContextMenu_CleanWorkspace_Click(object sender, EventArgs e)
		{
			if (!WaitForProgramsToFinish())
			{
				return;
			}

			string? ExtraSafeToDeleteFolders;
			if (!TryGetProjectSetting(Workspace.ProjectConfigFile, "SafeToDeleteFolders", out ExtraSafeToDeleteFolders))
			{
				ExtraSafeToDeleteFolders = "";
			}

			string? ExtraSafeToDeleteExtensions;
			if (!TryGetProjectSetting(Workspace.ProjectConfigFile, "SafeToDeleteExtensions", out ExtraSafeToDeleteExtensions))
			{
				ExtraSafeToDeleteExtensions = "";
			}

			string[] CombinedSyncFilter = UserSettings.GetCombinedSyncFilter(GetSyncCategories(), Settings.Global.Filter, WorkspaceSettings.Filter);
			List<string> SyncPaths = WorkspaceUpdate.GetSyncPaths(Workspace.Project, WorkspaceSettings.Filter.AllProjects ?? Settings.Global.Filter.AllProjects ?? false, CombinedSyncFilter);

			CleanWorkspaceWindow.DoClean(ParentForm, PerforceSettings, BranchDirectoryName, Workspace.Project.ClientRootPath, SyncPaths, ExtraSafeToDeleteFolders.Split('\n'), ExtraSafeToDeleteExtensions.Split('\n'), ServiceProvider.GetRequiredService<ILogger<CleanWorkspaceWindow>>());
		}

		private void UpdateBuildSteps()
		{
			bHasBuildSteps = false;

			int MoreToolsItemCount = MoreToolsContextMenu.Items.IndexOf(MoreActionsContextMenu_CustomToolSeparator);
			while (MoreToolsItemCount > 0)
			{
				MoreToolsContextMenu.Items.RemoveAt(--MoreToolsItemCount);
			}

			for (int Idx = 0; Idx < CustomStatusPanelMenus.Count; Idx++)
			{
				components.Remove(CustomStatusPanelMenus[Idx]);
				CustomStatusPanelMenus[Idx].Dispose();
			}

			CustomStatusPanelLinks.Clear();
			CustomStatusPanelMenus.Clear();

			if (Workspace != null)
			{
				ConfigFile ProjectConfigFile = Workspace.ProjectConfigFile;
				if (ProjectConfigFile != null)
				{
					Dictionary<Guid, ConfigObject> ProjectBuildStepObjects = GetProjectBuildStepObjects(ProjectConfigFile);
					List<BuildStep> UserSteps = GetUserBuildSteps(ProjectBuildStepObjects);

					Dictionary<string, ContextMenuStrip> NameToMenu = new Dictionary<string, ContextMenuStrip>();
					foreach (BuildStep Step in UserSteps)
					{
						if (!String.IsNullOrEmpty(Step.StatusPanelLink))
						{
							int BaseMenuIdx = Step.StatusPanelLink.IndexOf('|');
							if (BaseMenuIdx == -1)
							{
								CustomStatusPanelLinks.Add((Step.StatusPanelLink, (P, R) => RunCustomTool(Step, UserSteps)));
							}
							else
							{
								string MenuName = Step.StatusPanelLink.Substring(0, BaseMenuIdx);
								string ItemName = Step.StatusPanelLink.Substring(BaseMenuIdx + 1).Replace("&", "&&");

								ToolStripMenuItem NewMenuItem = new ToolStripMenuItem(ItemName);
								NewMenuItem.Click += new EventHandler((sender, e) => { RunCustomTool(Step, UserSteps); });

								if (MenuName == "More...")
								{
									MoreToolsContextMenu.Items.Insert(MoreToolsItemCount++, NewMenuItem);
								}
								else
								{
									ContextMenuStrip? Menu;
									if (!NameToMenu.TryGetValue(MenuName, out Menu))
									{
										Menu = new ContextMenuStrip();
										NameToMenu.Add(MenuName, Menu);
										CustomStatusPanelLinks.Add(($"{MenuName} \u25BE", (P, R) => ShowToolContextMenu(R, Menu)));
										CustomStatusPanelMenus.Add(Menu);
										components.Add(Menu);
									}
									Menu.Items.Add(NewMenuItem);
								}
							}
						}

						bHasBuildSteps |= Step.bNormalSync;
					}
				}
			}

			MoreActionsContextMenu_CustomToolSeparator.Visible = (MoreToolsItemCount > 0);
		}

		private void RunCustomTool(BuildStep Step, List<BuildStep> AllSteps)
		{
			if (Workspace != null)
			{
				if (Workspace.IsBusy())
				{
					MessageBox.Show("Please retry after the current sync has finished.", "Sync in Progress");
				}
				else
				{
					HashSet<Guid> StepSet = new HashSet<Guid> { Step.UniqueId };

					ConfigFile ProjectConfigFile = Workspace.ProjectConfigFile;
					if (ProjectConfigFile != null && Step.Requires.Length > 0)
					{
						Stack<Guid> Stack = new Stack<Guid>(StepSet);
						while (Stack.Count > 0)
						{
							Guid Id = Stack.Pop();
							BuildStep NextStep = AllSteps.FirstOrDefault(x => x.UniqueId == Id);
							if (NextStep != null)
							{
								foreach (Guid RequiresId in NextStep.Requires)
								{
									if (StepSet.Add(RequiresId))
									{
										Stack.Push(RequiresId);
									}
								}
							}
						}
					}

					WorkspaceUpdateContext Context = new WorkspaceUpdateContext(Workspace.CurrentChangeNumber, WorkspaceUpdateOptions.Build, GetEditorBuildConfig(), null, ProjectSettings.BuildSteps, StepSet);

					if (Step.ToolId != Guid.Empty)
					{
						string? ToolName = Owner.ToolUpdateMonitor.GetToolName(Step.ToolId);
						if (ToolName != null)
						{
							DirectoryReference? ToolPath = Owner.ToolUpdateMonitor.GetToolPath(ToolName);
							if (ToolPath != null)
							{
								Context.AdditionalVariables["ToolDir"] = ToolPath.FullName;
							}
						}
					}

					StartWorkspaceUpdate(Context, null);
				}
			}
		}

		private void OptionsContextMenu_EditBuildSteps_Click(object sender, EventArgs e)
		{
			ConfigFile ProjectConfigFile = Workspace.ProjectConfigFile;
			if (Workspace != null && ProjectConfigFile != null)
			{
				// Find all the target names for this project
				List<string> TargetNames = new List<string>();
				if (SelectedFileName != null && SelectedFileName.HasExtension(".uproject"))
				{
					DirectoryInfo SourceDirectory = DirectoryReference.Combine(SelectedFileName.Directory, "Source").ToDirectoryInfo();
					if (SourceDirectory.Exists)
					{
						foreach (FileInfo TargetFile in SourceDirectory.EnumerateFiles("*.target.cs", SearchOption.TopDirectoryOnly))
						{
							TargetNames.Add(TargetFile.Name.Substring(0, TargetFile.Name.IndexOf('.')));
						}
					}
				}

				// Get all the task objects
				Dictionary<Guid, ConfigObject> ProjectBuildStepObjects = GetProjectBuildStepObjects(ProjectConfigFile);
				List<BuildStep> UserSteps = GetUserBuildSteps(ProjectBuildStepObjects);

				// Show the dialog
				Dictionary<string, string> Variables = Workspace.GetVariables(GetEditorBuildConfig());
				ModifyBuildStepsWindow EditStepsWindow = new ModifyBuildStepsWindow(TargetNames, UserSteps, new HashSet<Guid>(ProjectBuildStepObjects.Keys), BranchDirectoryName, Variables);
				EditStepsWindow.ShowDialog();

				// Update the user settings
				List<ConfigObject> ModifiedBuildSteps = new List<ConfigObject>();
				foreach (BuildStep Step in UserSteps)
				{
					if (Step.IsValid())
					{
						ConfigObject? DefaultObject;
						ProjectBuildStepObjects.TryGetValue(Step.UniqueId, out DefaultObject);

						ConfigObject? UserConfigObject = Step.ToConfigObject(DefaultObject);
						if (UserConfigObject != null && UserConfigObject.Pairs.Any(x => x.Key != "UniqueId"))
						{
							ModifiedBuildSteps.Add(UserConfigObject);
						}
					}
				}

				// Save the settings
				ProjectSettings.BuildSteps = ModifiedBuildSteps;
				ProjectSettings.Save();

				// Update the custom tools menu, because we might have changed it
				UpdateBuildSteps();
				UpdateStatusPanel();
				UpdateSyncActionCheckboxes();
			}
		}

		private void AddOrUpdateBuildStep(Dictionary<Guid, ConfigObject> Steps, ConfigObject Object)
		{
			Guid UniqueId;
			if (Guid.TryParse(Object.GetValue(BuildStep.UniqueIdKey, ""), out UniqueId))
			{
				// Add or apply Object to the list of steps in Steps. Do not modify Object; make a copy first.
				ConfigObject NewObject = new ConfigObject(Object);

				ConfigObject? DefaultObject;
				if (Steps.TryGetValue(UniqueId, out DefaultObject))
				{
					NewObject.SetDefaults(DefaultObject);
				}

				Steps[UniqueId] = NewObject;
			}
		}

		private bool ShouldSyncPrecompiledEditor
		{
			get { return Settings.Archives.Any(x => x.bEnabled && x.Type == IArchiveInfo.EditorArchiveType) && GetArchives().Any(x => x.Type == "Editor"); }
		}

		public BuildConfig GetEditorBuildConfig()
		{
			return ShouldSyncPrecompiledEditor ? BuildConfig.Development : Settings.CompiledEditorBuildConfig;
		}

		private Dictionary<Guid, ConfigObject> GetProjectBuildStepObjects(ConfigFile ProjectConfigFile)
		{
			FileReference EditorTargetFile = ConfigUtils.GetEditorTargetFile(ProjectInfo, ProjectConfigFile);
			string EditorTargetName = EditorTargetFile.GetFileNameWithoutAnyExtensions();

			Dictionary<Guid, ConfigObject> ProjectBuildSteps = ConfigUtils.GetDefaultBuildStepObjects(ProjectInfo, EditorTargetName, Settings.CompiledEditorBuildConfig, ProjectConfigFile, ShouldSyncPrecompiledEditor);
			foreach (string Line in ProjectConfigFile.GetValues("Build.Step", new string[0]))
			{
				AddOrUpdateBuildStep(ProjectBuildSteps, new ConfigObject(Line));
			}
			return ProjectBuildSteps;
		}

		private List<BuildStep> GetUserBuildSteps(Dictionary<Guid, ConfigObject> ProjectBuildStepObjects)
		{
			// Read all the user-defined build tasks and modifications to the default list
			Dictionary<Guid, ConfigObject> UserBuildStepObjects = ProjectBuildStepObjects.ToDictionary(x => x.Key, y => new ConfigObject(y.Value));
			foreach (ConfigObject UserBuildStep in ProjectSettings.BuildSteps)
			{
				AddOrUpdateBuildStep(UserBuildStepObjects, UserBuildStep);
			}

			// Create the expanded task objects
			return UserBuildStepObjects.Values.Select(x => new BuildStep(x)).OrderBy(x => (x.OrderIndex == -1)? 10000 : x.OrderIndex).ToList();
		}

		private void OptionsContextMenu_SyncPrecompiledBinaries_Click(object sender, EventArgs e)
		{
			if (OptionsContextMenu_SyncPrecompiledBinaries.DropDownItems.Count == 0)
			{
				IArchiveInfo EditorArchive = GetArchives().FirstOrDefault(x => x.Type == IArchiveInfo.EditorArchiveType);
				if (EditorArchive != null)
				{
					SetSelectedArchive(EditorArchive, !OptionsContextMenu_SyncPrecompiledBinaries.Checked);
				}
			}
		}

		private void BuildList_SelectedIndexChanged(object sender, EventArgs e)
		{
			PendingSelectedChangeNumber = -1;
		}

		private void OptionsContextMenu_Diagnostics_Click(object sender, EventArgs e)
		{
			StringBuilder DiagnosticsText = new StringBuilder();
			DiagnosticsText.AppendFormat("Application version: {0}\n", Program.GetVersionString());
			DiagnosticsText.AppendFormat("Synced from: {0}\n", Program.SyncVersion ?? "(unknown)");
			DiagnosticsText.AppendFormat("Selected file: {0}\n", (SelectedFileName == null) ? "(none)" : SelectedFileName.FullName);
			if (Workspace != null)
			{
				DiagnosticsText.AppendFormat("P4 server: {0}\n", PerforceSettings.ServerAndPort ?? "(default)");
				DiagnosticsText.AppendFormat("P4 user: {0}\n", PerforceSettings.UserName);
				DiagnosticsText.AppendFormat("P4 workspace: {0}\n", PerforceSettings.ClientName);
			}
			DiagnosticsText.AppendFormat("Perforce monitor: {0}\n", (PerforceMonitor == null) ? "(inactive)" : PerforceMonitor.LastStatusMessage);
			DiagnosticsText.AppendFormat("Event monitor: {0}\n", (EventMonitor == null) ? "(inactive)" : EventMonitor.LastStatusMessage);
			DiagnosticsText.AppendFormat("Issue monitor: {0}\n", (IssueMonitor == null) ? "(inactive)" : IssueMonitor.LastStatusMessage);

			DiagnosticsWindow Diagnostics = new DiagnosticsWindow(AppDataFolder, WorkspaceDataFolder, DiagnosticsText.ToString(), Settings.GetCachedFilePaths());
			Diagnostics.ShowDialog(this);
		}

		private void OptionsContextMenu_SyncFilter_Click(object sender, EventArgs e)
		{
			SyncFilter Filter = new SyncFilter(GetSyncCategories(), Settings.Global.Filter, WorkspaceSettings.Filter);
			if (Filter.ShowDialog() == DialogResult.OK)
			{
				Settings.Global.Filter = Filter.GlobalFilter;
				Settings.Save();

				WorkspaceSettings.Filter = Filter.WorkspaceFilter;
				WorkspaceSettings.Save();
			}
		}

		/// <summary>
		/// Used to load CustomLatestChangeTypes config data into the windows form.
		/// </summary>
		private void RefreshSyncMenuItems()
		{
			SyncContextMenu.Items.Clear();
			SyncContextMenu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
			this.SyncContexMenu_EnterChangelist,
			this.toolStripSeparator8});

			foreach (LatestChangeType ChangeType in GetCustomLatestChangeTypes())
			{
				System.Windows.Forms.ToolStripMenuItem MenuItem = new System.Windows.Forms.ToolStripMenuItem();
				MenuItem.Name = ChangeType.Name;
				MenuItem.Text = ChangeType.Description;
				MenuItem.Size = new System.Drawing.Size(189, 22);
				MenuItem.Click += (sender, e) => this.SyncContextMenu_LatestChangeType_Click(sender, e, ChangeType.Name);

				SyncContextMenu.Items.Add(MenuItem);
			}
		}

		private void ShowSyncMenu(Rectangle Bounds)
		{
			RefreshSyncMenuItems();

			string? NameOfLatestChangeType = null;
			foreach (LatestChangeType ChangeType in GetCustomLatestChangeTypes())
			{
				if (ChangeType.Name == Settings.SyncTypeID)
				{
					NameOfLatestChangeType = ChangeType.Name;
					break;
				}
			}

			if (NameOfLatestChangeType != null)
			{
				foreach (int value in Enumerable.Range(0, SyncContextMenu.Items.Count))
				{
					System.Windows.Forms.ToolStripMenuItem? Item = SyncContextMenu.Items[value] as System.Windows.Forms.ToolStripMenuItem;
					if (Item != null)
					{
						if (Item.Name == NameOfLatestChangeType)
						{
							Item.Checked = true;
						}
						else
						{
							Item.Checked = false;
						}
					}
				}
			}

			SyncContextMenu.Show(StatusPanel, new Point(Bounds.Left, Bounds.Bottom), ToolStripDropDownDirection.BelowRight);
		}

		private void SyncContextMenu_LatestChangeType_Click(object? sender, EventArgs e, string SyncTypeID)
		{
			Settings.SyncTypeID = SyncTypeID;
			Settings.Save();
		}

		private void SyncContextMenu_EnterChangelist_Click(object sender, EventArgs e)
		{
			if (!WaitForProgramsToFinish())
			{
				return;
			}

			ChangelistWindow ChildWindow = new ChangelistWindow((Workspace == null) ? -1 : Workspace.CurrentChangeNumber);
			if (ChildWindow.ShowDialog() == DialogResult.OK)
			{
				StartSync(ChildWindow.ChangeNumber);
			}
		}

		private void BuildList_KeyDown(object Sender, KeyEventArgs Args)
		{
			if (Args.Control && Args.KeyCode == Keys.C && BuildList.SelectedItems.Count > 0)
			{
				int SelectedChange = ((ChangesRecord)BuildList.SelectedItems[0].Tag).Number;
				Clipboard.SetText(String.Format("{0}", SelectedChange));
			}
		}

		private void OptionsContextMenu_ApplicationSettings_Click(object sender, EventArgs e)
		{
			Owner.ModifyApplicationSettings();
		}

		private void OptionsContextMenu_TabNames_Stream_Click(object sender, EventArgs e)
		{
			Owner.SetTabNames(TabLabels.Stream);
		}

		private void OptionsContextMenu_TabNames_WorkspaceName_Click(object sender, EventArgs e)
		{
			Owner.SetTabNames(TabLabels.WorkspaceName);
		}

		private void OptionsContextMenu_TabNames_WorkspaceRoot_Click(object sender, EventArgs e)
		{
			Owner.SetTabNames(TabLabels.WorkspaceRoot);
		}

		private void OptionsContextMenu_TabNames_ProjectFile_Click(object sender, EventArgs e)
		{
			Owner.SetTabNames(TabLabels.ProjectFile);
		}

		private void SelectRecentProject(Rectangle Bounds)
		{
			while (RecentMenu.Items[2] != RecentMenu_Separator)
			{
				RecentMenu.Items.RemoveAt(2);
			}

			foreach (UserSelectedProjectSettings RecentProject in Settings.RecentProjects)
			{
				ToolStripMenuItem Item = new ToolStripMenuItem(RecentProject.ToString(), null, new EventHandler((o, e) => Owner.RequestProjectChange(this, RecentProject, true)));
				RecentMenu.Items.Insert(RecentMenu.Items.Count - 2, Item);
			}

			RecentMenu_Separator.Visible = (Settings.RecentProjects.Count > 0);
			RecentMenu.Show(StatusPanel, new Point(Bounds.Left, Bounds.Bottom), ToolStripDropDownDirection.BelowRight);
		}

		private void RecentMenu_Browse_Click(object sender, EventArgs e)
		{
			Owner.EditSelectedProject(this);
		}

		private void RecentMenu_ClearList_Click(object sender, EventArgs e)
		{
			Settings.RecentProjects.Clear();
			Settings.Save();
		}

		private void OptionsContextMenu_ShowChanges_ShowUnreviewed_Click(object sender, EventArgs e)
		{
			Settings.bShowUnreviewedChanges ^= true;
			Settings.Save();

			UpdateBuildList();
			ShrinkNumRequestedBuilds();
		}

		private void OptionsContextMenu_ShowChanges_ShowAutomated_Click(object sender, EventArgs e)
		{
			Settings.bShowAutomatedChanges ^= true;
			Settings.Save();

			UpdateBuildListFilter();
		}

		private void BuildList_ColumnWidthChanging(object sender, ColumnWidthChangingEventArgs e)
		{
			UpdateMaxBuildBadgeChars();
		}

		private void BuildList_ColumnWidthChanged(object sender, ColumnWidthChangedEventArgs e)
		{
			if (ColumnWidths != null && MinColumnWidths != null)
			{
				int NewWidth = BuildList.Columns[e.ColumnIndex].Width;
				if (NewWidth < MinColumnWidths[e.ColumnIndex])
				{
					NewWidth = MinColumnWidths[e.ColumnIndex];
					BuildList.Columns[e.ColumnIndex].Width = NewWidth;
				}
				ColumnWidths[e.ColumnIndex] = NewWidth;
			}
			UpdateMaxBuildBadgeChars();
		}

		private void BuildList_Resize(object sender, EventArgs e)
		{
			int PrevBuildListWidth = BuildListWidth;

			BuildListWidth = BuildList.Width;

			if (PrevBuildListWidth != 0 && BuildListWidth != PrevBuildListWidth && ColumnWidths != null)
			{
				float SafeWidth = ColumnWidths.Sum() + 50.0f;
				if (BuildListWidth < PrevBuildListWidth)
				{
					if (BuildListWidth <= SafeWidth)
					{
						ResizeColumns(ColumnWidths.Sum() + (BuildListWidth - Math.Max(PrevBuildListWidth, SafeWidth)));
					}
				}
				else
				{
					if (BuildListWidth >= SafeWidth)
					{
						ResizeColumns(ColumnWidths.Sum() + (BuildListWidth - Math.Max(PrevBuildListWidth, SafeWidth)));
					}
				}
			}
		}

		void ResizeColumns(float NextTotalWidth)
		{
			float[] TargetColumnWidths = GetTargetColumnWidths(NextTotalWidth);

			// Get the current total width of the columns, and the new space that we'll aim to fill
			float PrevTotalWidth = ColumnWidths.Sum();
			float TotalDelta = Math.Abs(NextTotalWidth - PrevTotalWidth);

			float TotalColumnDelta = 0.0f;
			for (int Idx = 0; Idx < BuildList.Columns.Count; Idx++)
			{
				TotalColumnDelta += Math.Abs(TargetColumnWidths[Idx] - ColumnWidths[Idx]);
			}

			if (TotalColumnDelta > 0.5f)
			{
				float[] NextColumnWidths = new float[BuildList.Columns.Count];
				for (int Idx = 0; Idx < BuildList.Columns.Count; Idx++)
				{
					float MaxColumnDelta = TotalDelta * Math.Abs(TargetColumnWidths[Idx] - ColumnWidths[Idx]) / TotalColumnDelta;
					NextColumnWidths[Idx] = Math.Max(Math.Min(TargetColumnWidths[Idx], ColumnWidths[Idx] + MaxColumnDelta), ColumnWidths[Idx] - MaxColumnDelta);
				}

				// Update the control
				BuildList.BeginUpdate();
				for (int Idx = 0; Idx < BuildList.Columns.Count; Idx++)
				{
					BuildList.Columns[Idx].Width = (int)NextColumnWidths[Idx];
				}
				ColumnWidths = NextColumnWidths;
				BuildList.EndUpdate();
			}
		}

		float[] GetTargetColumnWidths(float NextTotalWidth)
		{
			// Array to store the output list
			float[] TargetColumnWidths = new float[BuildList.Columns.Count];

			// Array of flags to store columns which are clamped into position. We try to respect proportional resizing as well as clamping to min/max sizes,
			// and remaining space can be distributed via non-clamped columns via another iteration.
			bool[] ConstrainedColumns = new bool[BuildList.Columns.Count];

			// Keep track of the remaining width that we have to distribute between columns. Does not include the required minimum size of each column.
			float RemainingWidth = Math.Max(NextTotalWidth - MinColumnWidths.Sum(), 0.0f);

			// Keep track of the sum of the remaining column weights. Used to proportionally allocate remaining space.
			float RemainingTotalWeight = ColumnWeights.Sum();

			// Handle special cases for shrinking/expanding
			float PrevTotalWidth = ColumnWidths.Sum();
			if (NextTotalWidth < PrevTotalWidth)
			{
				// If target size is less than current size, keep it at the current size
				for (int Idx = 0; Idx < BuildList.Columns.Count; Idx++)
				{
					if (!ConstrainedColumns[Idx])
					{
						float TargetColumnWidth = MinColumnWidths[Idx] + (RemainingWidth * ColumnWeights[Idx] / RemainingTotalWeight);
						if (TargetColumnWidth > ColumnWidths[Idx])
						{
							TargetColumnWidths[Idx] = ColumnWidths[Idx];
							ConstrainedColumns[Idx] = true;

							RemainingWidth -= Math.Max(TargetColumnWidths[Idx] - MinColumnWidths[Idx], 0.0f);
							RemainingTotalWeight -= ColumnWeights[Idx];

							Idx = -1;
							continue;
						}
					}
				}
			}
			else
			{
				// If target size is greater than desired size, clamp it to that
				for (int Idx = 0; Idx < BuildList.Columns.Count; Idx++)
				{
					if (!ConstrainedColumns[Idx])
					{
						float TargetColumnWidth = MinColumnWidths[Idx] + (RemainingWidth * ColumnWeights[Idx] / RemainingTotalWeight);
						if (TargetColumnWidth > DesiredColumnWidths[Idx])
						{
							// Don't allow this column to expand above the maximum desired size
							TargetColumnWidths[Idx] = DesiredColumnWidths[Idx];
							ConstrainedColumns[Idx] = true;

							RemainingWidth -= Math.Max(TargetColumnWidths[Idx] - MinColumnWidths[Idx], 0.0f);
							RemainingTotalWeight -= ColumnWeights[Idx];

							Idx = -1;
							continue;
						}
					}
				}

				// If current size is greater than target size, keep it that way
				for (int Idx = 0; Idx < BuildList.Columns.Count; Idx++)
				{
					if (!ConstrainedColumns[Idx])
					{
						float TargetColumnWidth = MinColumnWidths[Idx] + (RemainingWidth * ColumnWeights[Idx] / RemainingTotalWeight);
						if (TargetColumnWidth < ColumnWidths[Idx])
						{
							TargetColumnWidths[Idx] = ColumnWidths[Idx];
							ConstrainedColumns[Idx] = true;

							RemainingWidth -= Math.Max(TargetColumnWidths[Idx] - MinColumnWidths[Idx], 0.0f);
							RemainingTotalWeight -= ColumnWeights[Idx];

							Idx = -1;
							continue;
						}
					}
				}
			}

			// Allocate the remaining space equally
			for (int Idx = 0; Idx < BuildList.Columns.Count; Idx++)
			{
				if (!ConstrainedColumns[Idx])
				{
					TargetColumnWidths[Idx] = MinColumnWidths[Idx] + (RemainingWidth * ColumnWeights[Idx] / RemainingTotalWeight);
				}
			}

			return TargetColumnWidths;
		}

		private void BuidlListMultiContextMenu_Bisect_Click(object sender, EventArgs e)
		{
			EnableBisectMode();
		}

		private bool IsBisectModeEnabled()
		{
			return WorkspaceState.BisectChanges.Count >= 2;
		}

		private void EnableBisectMode()
		{
			if (BuildList.SelectedItems.Count >= 2)
			{
				Dictionary<int, BisectState> ChangeNumberToBisectState = new Dictionary<int, BisectState>();
				foreach (ListViewItem? SelectedItem in BuildList.SelectedItems)
				{
					if (SelectedItem != null)
					{
						ChangesRecord Change = (ChangesRecord)SelectedItem.Tag;
						ChangeNumberToBisectState[Change.Number] = BisectState.Include;
					}
				}

				ChangeNumberToBisectState[ChangeNumberToBisectState.Keys.Min()] = BisectState.Pass;
				ChangeNumberToBisectState[ChangeNumberToBisectState.Keys.Max()] = BisectState.Fail;

				WorkspaceState.BisectChanges = ChangeNumberToBisectState.Select(x => new BisectEntry {  Change = x.Key, State = x.Value }).ToList();
				WorkspaceState.Save();

				UpdateBuildList();
				UpdateStatusPanel();
			}
		}

		private void CancelBisectMode()
		{
			WorkspaceState.BisectChanges.Clear();
			WorkspaceState.Save();

			UpdateBuildList();
			UpdateStatusPanel();
		}

		private void SetBisectStateForSelection(BisectState State)
		{
			foreach (ListViewItem? SelectedItem in BuildList.SelectedItems)
			{
				ChangesRecord? Change = SelectedItem?.Tag as ChangesRecord;
				if (Change != null)
				{
					WorkspaceState.SetBisectState(Change.Number, State);
				}
			}

			WorkspaceState.Save();

			ChangeNumberToLayoutInfo.Clear();
			BuildList.Invalidate();

			UpdateStatusPanel();
		}

		private int GetBisectChangeNumber()
		{
			int PassChangeNumber;
			int FailChangeNumber;
			GetRemainingBisectRange(out PassChangeNumber, out FailChangeNumber);

			List<int> ChangeNumbers = new List<int>();
			foreach (BisectEntry Entry in WorkspaceState.BisectChanges)
			{
				if (Entry.State == BisectState.Include && Entry.Change > PassChangeNumber && Entry.Change < FailChangeNumber)
				{
					ChangeNumbers.Add(Entry.Change);
				}
			}

			ChangeNumbers.Sort();

			return (ChangeNumbers.Count > 0) ? ChangeNumbers[ChangeNumbers.Count / 2] : -1;
		}

		private void SyncBisectChange()
		{
			int BisectChange = GetBisectChangeNumber();
			if (BisectChange != -1)
			{
				Owner.ShowAndActivate();
				SelectChange(BisectChange);
				StartSync(BisectChange);
			}
		}

		private void BuildListContextMenu_Bisect_Pass_Click(object sender, EventArgs e)
		{
			SetBisectStateForSelection(BisectState.Pass);
		}

		private void BuildListContextMenu_Bisect_Fail_Click(object sender, EventArgs e)
		{
			SetBisectStateForSelection(BisectState.Fail);
		}

		private void BuildListContextMenu_Bisect_Exclude_Click(object sender, EventArgs e)
		{
			SetBisectStateForSelection(BisectState.Exclude);
		}

		private void BuildListContextMenu_Bisect_Include_Click(object sender, EventArgs e)
		{
			SetBisectStateForSelection(BisectState.Include);
		}

		private void WorkspaceControl_VisibleChanged(object sender, EventArgs e)
		{
			if (PerforceMonitor != null && PerforceMonitor.IsActive != Visible)
			{
				PerforceMonitor.IsActive = Visible;
			}
		}

		private void FilterButton_Click(object sender, EventArgs e)
		{
			FilterContextMenu_Default.Checked = !Settings.bShowAutomatedChanges && ProjectSettings.FilterType == FilterType.None && ProjectSettings.FilterBadges.Count == 0;

			FilterContextMenu_Type.Checked = ProjectSettings.FilterType != FilterType.None;
			FilterContextMenu_Type_ShowAll.Checked = ProjectSettings.FilterType == FilterType.None;
			FilterContextMenu_Type_Code.Checked = ProjectSettings.FilterType == FilterType.Code;
			FilterContextMenu_Type_Content.Checked = ProjectSettings.FilterType == FilterType.Content;

			FilterContextMenu_Badges.DropDownItems.Clear();
			FilterContextMenu_Badges.Checked = ProjectSettings.FilterBadges.Count > 0;

			HashSet<string> BadgeNames = new HashSet<string>(ProjectSettings.FilterBadges, StringComparer.OrdinalIgnoreCase);
			BadgeNames.ExceptWith(BadgeNameAndGroupPairs.Select(x => x.Key));

			List<KeyValuePair<string, string>> DisplayBadgeNameAndGroupPairs = new List<KeyValuePair<string, string>>(BadgeNameAndGroupPairs);
			DisplayBadgeNameAndGroupPairs.AddRange(BadgeNames.Select(x => new KeyValuePair<string, string>(x, "User")));

			string? LastGroup = null;
			foreach (KeyValuePair<string, string> BadgeNameAndGroupPair in DisplayBadgeNameAndGroupPairs)
			{
				if (LastGroup != BadgeNameAndGroupPair.Value)
				{
					if (LastGroup != null)
					{
						FilterContextMenu_Badges.DropDownItems.Add(new ToolStripSeparator());
					}
					LastGroup = BadgeNameAndGroupPair.Value;
				}

				ToolStripMenuItem Item = new ToolStripMenuItem(BadgeNameAndGroupPair.Key);
				Item.Checked = ProjectSettings.FilterBadges.Contains(BadgeNameAndGroupPair.Key, StringComparer.OrdinalIgnoreCase);
				Item.Click += (Sender, Args) => FilterContextMenu_Badge_Click(BadgeNameAndGroupPair.Key);
				FilterContextMenu_Badges.DropDownItems.Add(Item);
			}

			FilterContextMenu_Badges.Enabled = FilterContextMenu_Badges.DropDownItems.Count > 0;

			FilterContextMenu_ShowBuildMachineChanges.Checked = Settings.bShowAutomatedChanges;

			// Set checks if any robomerge filters are applied
			FilterContextMenu_Robomerge.Checked = Settings.ShowRobomerge != UserSettings.RobomergeShowChangesOption.All || Settings.bAnnotateRobmergeChanges;
			FilterContextMenu_Robomerge_ShowAll.Checked = Settings.ShowRobomerge == UserSettings.RobomergeShowChangesOption.All;
			FilterContextMenu_Robomerge_ShowBadged.Checked = Settings.ShowRobomerge == UserSettings.RobomergeShowChangesOption.Badged;
			FilterContextMenu_Robomerge_Annotate.Checked = Settings.bAnnotateRobmergeChanges;

			// Set checks if an author filter string is set
			FilterContextMenu_Author.Checked = !string.IsNullOrEmpty(this.AuthorFilterText) && this.AuthorFilterText != AuthorFilterPlaceholderText;

			FilterContextMenu.Show(FilterButton, new Point(0, FilterButton.Height));
		}

		private void FilterContextMenu_Badge_Click(string BadgeName)
		{
			if (ProjectSettings.FilterBadges.Contains(BadgeName))
			{
				ProjectSettings.FilterBadges.Remove(BadgeName);
			}
			else
			{
				ProjectSettings.FilterBadges.Add(BadgeName);
			}
			Settings.Save();

			UpdateBuildListFilter();
		}

		private void FilterContextMenu_Default_Click(object sender, EventArgs e)
		{
			ProjectSettings.FilterBadges.Clear();
			ProjectSettings.FilterType = FilterType.None;
			ProjectSettings.Save();

			Settings.bShowAutomatedChanges = false;
			Settings.Save();

			UpdateBuildListFilter();
		}

		private void FilterContextMenu_ShowBuildMachineChanges_Click(object sender, EventArgs e)
		{
			Settings.bShowAutomatedChanges ^= true;
			Settings.Save();

			UpdateBuildListFilter();
		}

		private void FilterContextMenu_Robomerge_ShowAll_Click(object sender, EventArgs e)
		{
			Settings.ShowRobomerge = UserSettings.RobomergeShowChangesOption.All;	
			Settings.Save();
			UpdateBuildListFilter();
		}

		private void FilterContextMenu_Robomerge_ShowBadged_Click(object sender, EventArgs e)
		{
			Settings.ShowRobomerge = UserSettings.RobomergeShowChangesOption.Badged;
			Settings.Save();
			UpdateBuildListFilter();
		}

		private void FilterContextMenu_Robomerge_ShowNone_Click(object sender, EventArgs e)
		{
			Settings.ShowRobomerge = UserSettings.RobomergeShowChangesOption.None;
			Settings.Save();
			UpdateBuildListFilter();
		}

		private void FilterContextMenu_Robomerge_Annotate_Click(object sender, EventArgs e)
		{
			Settings.bAnnotateRobmergeChanges ^= true;
			Settings.Save();
			BuildList.Items.Clear();	// need to reload to update user names

			UpdateBuildListFilter();
		}

		/// <summary>
		/// Handler for the edit box of the author filtering taking focus. This clears any
		/// placeholder text and sets the foreground color to black so it shows as active
		/// </summary>
		/// <param name="sender"></param>
		/// <param name="e"></param>
		private void FilterContextMenu_Author_Name_GotFocus(object sender, EventArgs e)
		{
			if (this.FilterContextMenu_Author_Name.Text == AuthorFilterPlaceholderText)
			{
				this.FilterContextMenu_Author_Name.Text = "";
				this.FilterContextMenu_Author_Name.TextBox.ForeColor = Color.Black;
			}
		}

		/// <summary>
		/// Handler for the edit box of the author filter losing focus. If the filter box
		/// is empty we restore the placeholder text and state
		/// </summary>
		/// <param name="sender"></param>
		/// <param name="e"></param>
		private void FilterContextMenu_Author_Name_LostFocus(object sender, EventArgs e)
		{
			if (string.IsNullOrEmpty(this.FilterContextMenu_Author_Name.Text))
			{
				this.FilterContextMenu_Author_Name.Text = AuthorFilterPlaceholderText;
				this.FilterContextMenu_Author_Name.TextBox.ForeColor = Color.DarkGray;
			}
		}

		/// <summary>
		/// Handler for the author name changing. We save off the text and update the 
		/// filter string
		/// </summary>
		/// <param name="sender"></param>
		/// <param name="e"></param>
		private void FilterContextMenu_Author_Name_Changed(object sender, EventArgs e)
		{
			AuthorFilterText = this.FilterContextMenu_Author_Name.Text;
			UpdateBuildListFilter();
		}


		private void FilterContextMenu_Type_ShowAll_Click(object sender, EventArgs e)
		{
			ProjectSettings.FilterType = FilterType.None;
			ProjectSettings.Save();

			UpdateBuildListFilter();
		}

		private void FilterContextMenu_Type_Code_Click(object sender, EventArgs e)
		{
			ProjectSettings.FilterType = FilterType.Code;
			ProjectSettings.Save();

			UpdateBuildListFilter();
		}

		private void FilterContextMenu_Type_Content_Click(object sender, EventArgs e)
		{
			ProjectSettings.FilterType = FilterType.Content;
			ProjectSettings.Save();

			UpdateBuildListFilter();
		}

		private void UpdateBuildListFilter()
		{
			UpdateBuildList();
			ShrinkNumRequestedBuilds();
		}

		public Color? TintColor
		{
			get;
			private set;
		}

		private void EditorConfigWatcher_Changed(object sender, FileSystemEventArgs e)
		{
			UpdateTintColor();
		}

		private void EditorConfigWatcher_Renamed(object sender, RenamedEventArgs e)
		{
			UpdateTintColor();
		}

		private Color? GetTintColor()
		{
			try
			{
				return GetNewTintColor() ?? GetLegacyTintColor();
			}
			catch
			{
				return null;
			}
		}

		private Color? GetNewTintColor()
		{
			FileReference NewConfigFile = FileReference.Combine(SelectedFileName.Directory, "Saved", "Config", "WindowsEditor", "EditorPerProjectUserSettings.ini");
			if (!FileReference.Exists(NewConfigFile))
			{
				return null;
			}

			ConfigFile Config = new ConfigFile();
			Config.Load(NewConfigFile);

			ConfigSection Section = Config.FindSection("/Script/EditorStyle.EditorStyleSettings");
			if (Section == null)
			{
				return null;
			}

			string? EnableColor = Section.GetValue("bEnableEditorWindowBackgroundColor", null);
			if (EnableColor == null || !String.Equals(EnableColor, "True", StringComparison.OrdinalIgnoreCase))
			{
				return null;
			}

			string? BackgroundColor = Section.GetValue("EditorWindowBackgroundColor", null);
			if (BackgroundColor == null)
			{
				return null;
			}

			ConfigObject OverrideObject = new ConfigObject(BackgroundColor);

			float R, G, B;
			if (!float.TryParse(OverrideObject.GetValue("R", ""), out R) || !float.TryParse(OverrideObject.GetValue("G", ""), out G) || !float.TryParse(OverrideObject.GetValue("B", ""), out B))
			{
				return null;
			}

			return Color.FromArgb((int)(255.0f * R), (int)(255.0f * G), (int)(255.0f * B));
		}

		private Color? GetLegacyTintColor()
		{
			FileReference FileName = FileReference.Combine(SelectedFileName.Directory, "Saved", "Config", "Windows", "EditorPerProjectUserSettings.ini");
			if (!FileReference.Exists(FileName))
			{
				return null;
			}

			ConfigFile Config = new ConfigFile();
			Config.Load(FileName);

			ConfigSection Section = Config.FindSection("/Script/EditorStyle.EditorStyleSettings");
			if (Section == null)
			{
				return null;
			}

			string? OverrideValue = Section.GetValue("EditorMainWindowBackgroundOverride", null);
			if (OverrideValue == null)
			{
				return null;
			}

			ConfigObject OverrideObject = new ConfigObject(OverrideValue);

			string? TintColorValue = OverrideObject.GetValue("TintColor");
			if (TintColorValue == null)
			{
				return null;
			}

			ConfigObject TintColorObject = new ConfigObject(TintColorValue);
			if (TintColorObject.GetValue("ColorUseRule", "") != "UseColor_Specified")
			{
				return null;
			}

			string? SpecifiedColorValue = TintColorObject.GetValue("SpecifiedColor");
			if (SpecifiedColorValue == null)
			{
				return null;
			}

			ConfigObject SpecifiedColorObject = new ConfigObject(SpecifiedColorValue);

			float R, G, B;
			if (!float.TryParse(SpecifiedColorObject.GetValue("R", ""), out R) || !float.TryParse(SpecifiedColorObject.GetValue("G", ""), out G) || !float.TryParse(SpecifiedColorObject.GetValue("B", ""), out B))
			{
				return null;
			}

			return Color.FromArgb((int)(255.0f * R), (int)(255.0f * G), (int)(255.0f * B));
		}

		private void UpdateTintColor()
		{
			Color? NewTintColor = GetTintColor();
			if (TintColor != NewTintColor)
			{
				TintColor = NewTintColor;
				Owner.UpdateTintColors();
			}
		}
	}
}
