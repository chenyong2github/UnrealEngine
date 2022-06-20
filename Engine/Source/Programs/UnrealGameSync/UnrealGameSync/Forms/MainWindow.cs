// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using System.Threading;
using System.Reflection;
using System.Text;
using System.Diagnostics;
using UnrealGameSync.Forms;
using System.Text.RegularExpressions;
using EpicGames.Core;
using EpicGames.Perforce;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.DependencyInjection;
using System.Diagnostics.CodeAnalysis;

namespace UnrealGameSync
{
	interface IMainWindowTabPanel : IDisposable
	{
		void Activate();
		void Deactivate();
		void Hide();
		void Show();
		bool IsBusy();
		bool CanClose();
		bool CanSyncNow();
		void SyncLatestChange();
		bool CanLaunchEditor();
		void LaunchEditor();
		void UpdateSettings();

		Color? TintColor
		{
			get;
		}

		Tuple<TaskbarState, float> DesiredTaskbarState
		{
			get;
		}

		UserSelectedProjectSettings SelectedProject
		{
			get;
		}
	}

	partial class MainWindow : Form, IWorkspaceControlOwner
	{
		class WorkspaceIssueMonitor
		{
			public IssueMonitor IssueMonitor;
			public int RefCount;

			public WorkspaceIssueMonitor(IssueMonitor IssueMonitor)
			{
				this.IssueMonitor = IssueMonitor;
			}
		}

		[Flags]
		enum OpenProjectOptions
		{
			None,
			Quiet,
		}

		[DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
		static extern int SetWindowTheme(IntPtr hWnd, string pszSubAppName, string pszSubIdList);

		[DllImport("user32.dll")]
		public static extern int SendMessage(IntPtr hWnd, Int32 wMsg, Int32 wParam, Int32 lParam);

		private const int WM_SETREDRAW = 11; 

		UpdateMonitor UpdateMonitor;
		SynchronizationContext MainThreadSynchronizationContext;
		List<IssueMonitor> DefaultIssueMonitors = new List<IssueMonitor>();
		List<WorkspaceIssueMonitor> WorkspaceIssueMonitors = new List<WorkspaceIssueMonitor>();

		string? ApiUrl;
		DirectoryReference DataFolder;
		DirectoryReference CacheFolder;
		IPerforceSettings DefaultPerforceSettings;
		IServiceProvider ServiceProvider;
		ILogger Logger;
		UserSettings Settings;
		int TabMenu_TabIdx = -1;
		int ChangingWorkspacesRefCount;

		bool bAllowClose = false;

		bool bRestoreStateOnLoad;

		OIDCTokenManager? OIDCTokenManager;

		System.Threading.Timer? ScheduleTimer;
		System.Threading.Timer? ScheduleSettledTimer;

		string OriginalExecutableFileName;
		bool bPreview;

		IMainWindowTabPanel? CurrentTabPanel;

		AutomationServer AutomationServer;

		bool bAllowCreatingHandle;

		Rectangle PrimaryWorkArea;
		List<IssueAlertWindow> AlertWindows = new List<IssueAlertWindow>();

		public ToolUpdateMonitor ToolUpdateMonitor { get; private set; }

		public MainWindow(UpdateMonitor InUpdateMonitor, string? InApiUrl, DirectoryReference InDataFolder, DirectoryReference InCacheFolder, bool bInRestoreStateOnLoad, string InOriginalExecutableFileName, bool bInPreview, List<(UserSelectedProjectSettings, ModalTask<OpenProjectInfo>)> StartupTasks, IPerforceSettings InDefaultPerforceSettings, IServiceProvider InServiceProvider, UserSettings InSettings, string? InUri, OIDCTokenManager? InOidcTokenManager)
		{
			ServiceProvider = InServiceProvider;
			Logger = ServiceProvider.GetRequiredService<ILogger<MainWindow>>();

			Logger.LogInformation("Opening Main Window for {NumProject} projects. Last Project {LastProject}", StartupTasks.Count, InSettings.LastProject);

			InitializeComponent();

			UpdateMonitor = InUpdateMonitor;
			MainThreadSynchronizationContext = SynchronizationContext.Current!;
			ApiUrl = InApiUrl;
			DataFolder = InDataFolder;
			CacheFolder = InCacheFolder;
			bRestoreStateOnLoad = bInRestoreStateOnLoad;
			OriginalExecutableFileName = InOriginalExecutableFileName;
			bPreview = bInPreview;
			DefaultPerforceSettings = InDefaultPerforceSettings;
			ToolUpdateMonitor = new ToolUpdateMonitor(DefaultPerforceSettings, DataFolder, InSettings, ServiceProvider);

			Settings = InSettings;
			OIDCTokenManager = InOidcTokenManager;

			// While creating tab controls during startup, we need to prevent layout calls resulting in the window handle being created too early. Disable layout calls here.
			SuspendLayout();
			TabPanel.SuspendLayout();

			TabControl.OnTabChanged += TabControl_OnTabChanged;
			TabControl.OnNewTabClick += TabControl_OnNewTabClick;
			TabControl.OnTabClicked += TabControl_OnTabClicked;
			TabControl.OnTabClosing += TabControl_OnTabClosing;
			TabControl.OnTabClosed += TabControl_OnTabClosed;
			TabControl.OnTabReorder += TabControl_OnTabReorder;
			TabControl.OnButtonClick += TabControl_OnButtonClick;

			SetupDefaultControl();

			int SelectTabIdx = -1;
			foreach((UserSelectedProjectSettings Project, ModalTask<OpenProjectInfo> StartupTask) in StartupTasks)
			{
				int TabIdx = -1;
				if (StartupTask.Succeeded)
				{
					TabIdx = TryOpenProject(StartupTask.Result, -1, OpenProjectOptions.Quiet);
				}
				else if(StartupTask.Failed)
				{
					Logger.LogError("StartupProject Error: {Message}", StartupTask.Error);
					CreateErrorPanel(-1, Project, StartupTask.Error);
				}

				if (TabIdx != -1 && Settings.LastProject != null && Project.Equals(Settings.LastProject))
				{
					SelectTabIdx = TabIdx;
				}
			}

			if(SelectTabIdx != -1)
			{
				TabControl.SelectTab(SelectTabIdx);
			}
			else if(TabControl.GetTabCount() > 0)
			{
				TabControl.SelectTab(0);
			}

			StartScheduleTimer();

			if(bPreview)
			{
				Text += $" {Program.GetVersionString()} (UNSTABLE)";
			}

			ILogger<AutomationServer> AutomationLogger = ServiceProvider.GetRequiredService<ILogger<AutomationServer>>();
			AutomationServer = new AutomationServer(Request => { MainThreadSynchronizationContext.Post(Obj => PostAutomationRequest(Request), null); }, InUri, AutomationLogger);

			// Allow creating controls from now on
			TabPanel.ResumeLayout(false);
			ResumeLayout(false);

			foreach (string DefaultIssueApiUrl in DeploymentSettings.DefaultIssueApiUrls)
			{
				DefaultIssueMonitors.Add(CreateIssueMonitor(DefaultIssueApiUrl, InDefaultPerforceSettings.UserName));
			}

			bAllowCreatingHandle = true;

			foreach(WorkspaceIssueMonitor WorkspaceIssueMonitor in WorkspaceIssueMonitors)
			{
				WorkspaceIssueMonitor.IssueMonitor.Start();
			}

			ToolUpdateMonitor.Start();
		}

		void PostAutomationRequest(AutomationRequest Request)
		{
			try
			{
				if (!CanFocus)
				{
					Request.SetOutput(new AutomationRequestOutput(AutomationRequestResult.Busy));
				}
				else if (Request.Input.Type == AutomationRequestType.SyncProject)
				{
					AutomationRequestOutput? Output = StartAutomatedSync(Request, true);
					if (Output != null)
					{
						Request.SetOutput(Output);
					}
				}
				else if (Request.Input.Type == AutomationRequestType.FindProject)
				{
					AutomationRequestOutput Output = FindProject(Request);
					Request.SetOutput(Output);
				}
				else if (Request.Input.Type == AutomationRequestType.OpenProject)
				{
					AutomationRequestOutput? Output = StartAutomatedSync(Request, false);
					if (Output != null)
					{
						Request.SetOutput(Output);
					}
				}
				else if (Request.Input.Type == AutomationRequestType.ExecCommand)
				{
					AutomationRequestOutput Output = StartExecCommand(Request);
					Request.SetOutput(Output);
				}
				else if (Request.Input.Type == AutomationRequestType.OpenIssue)
				{
					AutomationRequestOutput Output = OpenIssue(Request);
					Request.SetOutput(new AutomationRequestOutput(AutomationRequestResult.Ok));
				}
				else
				{
					Request.SetOutput(new AutomationRequestOutput(AutomationRequestResult.Invalid));
				}
			}
			catch(Exception Ex)
			{
				Logger.LogError(Ex, "Exception running automation request");
				Request.SetOutput(new AutomationRequestOutput(AutomationRequestResult.Invalid));
			}
		}

		AutomationRequestOutput StartExecCommand(AutomationRequest Request)
		{
			BinaryReader Reader = new BinaryReader(new MemoryStream(Request.Input.Data));
			string StreamName = Reader.ReadString();
			int Changelist = Reader.ReadInt32();
			string Command = Reader.ReadString();
			string ProjectPath = Reader.ReadString();

			AutomatedBuildWindow.BuildInfo? BuildInfo;
			if (!AutomatedBuildWindow.ShowModal(this, DefaultPerforceSettings, StreamName, ProjectPath, Changelist, Command.ToString(), Settings, ServiceProvider, out BuildInfo))
			{
				return new AutomationRequestOutput(AutomationRequestResult.Canceled);
			}

			WorkspaceControl? Workspace;
			if (!OpenWorkspaceForAutomation(BuildInfo.SelectedWorkspaceInfo, StreamName, BuildInfo.ProjectPath, out Workspace))
			{
				return new AutomationRequestOutput(AutomationRequestResult.Error);
			}

			Workspace.AddStartupCallback((Control, bCancel) => StartExecCommandAfterStartup(Control, bCancel, BuildInfo.bSync? Changelist : -1, BuildInfo.ExecCommand));
			return new AutomationRequestOutput(AutomationRequestResult.Ok);
		}

		private void StartExecCommandAfterStartup(WorkspaceControl Workspace, bool bCancel, int Changelist, string Command)
		{
			if (!bCancel)
			{
				if(Changelist == -1)
				{
					StartExecCommandAfterSync(Workspace, WorkspaceUpdateResult.Success, Command);
				}
				else
				{
					Workspace.SyncChange(Changelist, true, Result => StartExecCommandAfterSync(Workspace, Result, Command));
				}
			}
		}

		private void StartExecCommandAfterSync(WorkspaceControl Workspace, WorkspaceUpdateResult Result, string Command)
		{
			if (Result == WorkspaceUpdateResult.Success && Command != null)
			{
				string CmdExe = Environment.GetEnvironmentVariable("COMSPEC") ?? "C:\\Windows\\System32\\cmd.exe";
				Workspace.ExecCommand("Run build command", "Running build command", CmdExe, String.Format("/c {0}", Command), Workspace.BranchDirectoryName.FullName, true);
			}
		}

		AutomationRequestOutput? StartAutomatedSync(AutomationRequest Request, bool bForceSync)
		{
			ShowAndActivate();

			BinaryReader Reader = new BinaryReader(new MemoryStream(Request.Input.Data));
			string StreamName = Reader.ReadString();
			string ProjectPath = Reader.ReadString();

			AutomatedSyncWindow.WorkspaceInfo? WorkspaceInfo;
			if(!AutomatedSyncWindow.ShowModal(this, DefaultPerforceSettings, StreamName, ProjectPath, out WorkspaceInfo, ServiceProvider))
			{
				return new AutomationRequestOutput(AutomationRequestResult.Canceled);
			}

			WorkspaceControl? Workspace;
			if(!OpenWorkspaceForAutomation(WorkspaceInfo, StreamName, ProjectPath, out Workspace))
			{
				return new AutomationRequestOutput(AutomationRequestResult.Error);
			}

			if(!bForceSync && Workspace.CanLaunchEditor())
			{
				return new AutomationRequestOutput(AutomationRequestResult.Ok, Encoding.UTF8.GetBytes(Workspace.SelectedFileName.FullName));
			}

			Workspace.AddStartupCallback((Control, bCancel) => StartAutomatedSyncAfterStartup(Control, bCancel, Request));
			return null;
		}

		private bool OpenWorkspaceForAutomation(AutomatedSyncWindow.WorkspaceInfo WorkspaceInfo, string StreamName, string ProjectPath, [NotNullWhen(true)] out WorkspaceControl? OutWorkspace)
		{
			if (WorkspaceInfo.bRequiresStreamSwitch)
			{
				// Close any tab containing this window
				for (int ExistingTabIdx = 0; ExistingTabIdx < TabControl.GetTabCount(); ExistingTabIdx++)
				{
					WorkspaceControl? ExistingWorkspace = TabControl.GetTabData(ExistingTabIdx) as WorkspaceControl;
					if (ExistingWorkspace != null && ExistingWorkspace.ClientName.Equals(WorkspaceInfo.WorkspaceName))
					{
						TabControl.RemoveTab(ExistingTabIdx);
						break;
					}
				}

				// Switch the stream
				Func<IPerforceConnection, CancellationToken, Task> SwitchTask = async (Connection, CancellationToken) =>
				{
					await Connection.SwitchClientToStreamAsync(StreamName, SwitchClientOptions.None, CancellationToken);
				};

				PerforceSettings Settings = new PerforceSettings(WorkspaceInfo.ServerAndPort, WorkspaceInfo.UserName);
				Settings.ClientName = WorkspaceInfo.WorkspaceName;

				ModalTask? Result = PerforceModalTask.Execute(Owner, "Please wait", "Switching streams, please wait...", Settings, SwitchTask, Logger, ModalTaskFlags.Quiet);
				if (Result == null || !Result.Succeeded)
				{
					Logger.LogError("Unable to switch stream ({Message})", Result?.Error ?? "Operation cancelled");
					OutWorkspace = null;
					return false;
				}
			}

			UserSelectedProjectSettings SelectedProject = new UserSelectedProjectSettings(WorkspaceInfo.ServerAndPort, WorkspaceInfo.UserName, UserSelectedProjectType.Client, String.Format("//{0}{1}", WorkspaceInfo.WorkspaceName, ProjectPath), null);

			int TabIdx = TryOpenProject(SelectedProject, -1, OpenProjectOptions.None);
			if (TabIdx == -1)
			{
				Logger.LogError("Unable to open project");
				OutWorkspace = null;
				return false;
			}

			WorkspaceControl? Workspace = TabControl.GetTabData(TabIdx) as WorkspaceControl;
			if (Workspace == null)
			{
				Logger.LogError("Workspace was unable to open");
				OutWorkspace = null;
				return false;
			}

			TabControl.SelectTab(TabIdx);
			OutWorkspace = Workspace;
			return true;
		}

		private void StartAutomatedSyncAfterStartup(WorkspaceControl Workspace, bool bCancel, AutomationRequest Request)
		{
			if(bCancel)
			{
				Request.SetOutput(new AutomationRequestOutput(AutomationRequestResult.Canceled));
			}
			else
			{
				Workspace.SyncLatestChange(Result => CompleteAutomatedSync(Result, Workspace.SelectedFileName, Request));
			}
		}

		void CompleteAutomatedSync(WorkspaceUpdateResult Result, FileReference SelectedFileName, AutomationRequest Request)
		{
			if(Result == WorkspaceUpdateResult.Success)
			{
				Request.SetOutput(new AutomationRequestOutput(AutomationRequestResult.Ok, Encoding.UTF8.GetBytes(SelectedFileName.FullName)));
			}
			else if(Result == WorkspaceUpdateResult.Canceled)
			{
				Request.SetOutput(new AutomationRequestOutput(AutomationRequestResult.Canceled));
			}
			else
			{
				Request.SetOutput(new AutomationRequestOutput(AutomationRequestResult.Error));
			}
		}

		AutomationRequestOutput FindProject(AutomationRequest Request)
		{
			BinaryReader Reader = new BinaryReader(new MemoryStream(Request.Input.Data));
			string StreamName = Reader.ReadString();
			string ProjectPath = Reader.ReadString();

			for(int ExistingTabIdx = 0; ExistingTabIdx < TabControl.GetTabCount(); ExistingTabIdx++)
			{
				WorkspaceControl? ExistingWorkspace = TabControl.GetTabData(ExistingTabIdx) as WorkspaceControl;
				if(ExistingWorkspace != null && String.Compare(ExistingWorkspace.StreamName, StreamName, StringComparison.OrdinalIgnoreCase) == 0 && ExistingWorkspace.SelectedProject != null)
				{
					string? ClientPath = ExistingWorkspace.SelectedProject.ClientPath;
					if(ClientPath != null && ClientPath.StartsWith("//"))
					{
						int SlashIdx = ClientPath.IndexOf('/', 2);
						if(SlashIdx != -1)
						{
							string ExistingProjectPath = ClientPath.Substring(SlashIdx);
							if(String.Compare(ExistingProjectPath, ProjectPath, StringComparison.OrdinalIgnoreCase) == 0)
							{
								return new AutomationRequestOutput(AutomationRequestResult.Ok, Encoding.UTF8.GetBytes(ExistingWorkspace.SelectedFileName.FullName));
							}
						}
					}
				}
			}

			return new AutomationRequestOutput(AutomationRequestResult.NotFound);
		}

		AutomationRequestOutput OpenIssue(AutomationRequest Request)
		{
			BinaryReader Reader = new BinaryReader(new MemoryStream(Request.Input.Data));
			int IssueId = Reader.ReadInt32();

			for (int ExistingTabIdx = 0; ExistingTabIdx < TabControl.GetTabCount(); ExistingTabIdx++)
			{
				WorkspaceControl? ExistingWorkspace = TabControl.GetTabData(ExistingTabIdx) as WorkspaceControl;
				if (ExistingWorkspace != null)
				{
					IssueMonitor IssueMonitor = ExistingWorkspace.GetIssueMonitor();
					if (IssueMonitor != null && (DeploymentSettings.UrlHandleIssueApi == null || String.Compare(IssueMonitor.ApiUrl, DeploymentSettings.UrlHandleIssueApi, StringComparison.OrdinalIgnoreCase) == 0))
					{
						IssueMonitor.AddRef();
						try
						{
							Func<CancellationToken, Task<IssueData>> Func = x => RESTApi.GetAsync<IssueData>($"{IssueMonitor.ApiUrl}/api/issues/{IssueId}", x);
							ModalTask<IssueData>? IssueTask = ModalTask.Execute(this, "Finding Issue", "Querying issue data, please wait...", Func);
							if (IssueTask == null)
							{
								Logger.LogInformation("Operation cancelled");
								return new AutomationRequestOutput(AutomationRequestResult.Canceled);
							}
							else if (IssueTask.Succeeded)
							{
								ExistingWorkspace.ShowIssueDetails(IssueTask.Result);
								return new AutomationRequestOutput(AutomationRequestResult.Ok);
							}
							else
							{
								Logger.LogError(IssueTask?.Exception, "Unable to query issue {IssueId} from {ApiUrl}: {Error}", IssueId, IssueMonitor.ApiUrl, IssueTask?.Error);
								return new AutomationRequestOutput(AutomationRequestResult.Error);
							}
						}
						finally
						{
							IssueMonitor.Release();
						}
					}
				}
			}

			return new AutomationRequestOutput(AutomationRequestResult.NotFound);
		}

		protected override void OnHandleCreated(EventArgs e)
		{
			base.OnHandleCreated(e);

			Debug.Assert(bAllowCreatingHandle, "Window handle should not be created before constructor has run.");
		}

		void TabControl_OnButtonClick(int ButtonIdx, Point Location, MouseButtons Buttons)
		{
			if(ButtonIdx == 0)
			{
				EditSelectedProject(TabControl.GetSelectedTabIndex());
			}
		}

		void TabControl_OnTabClicked(object? TabData, Point Location, MouseButtons Buttons)
		{
			if(Buttons == System.Windows.Forms.MouseButtons.Right)
			{
				Activate();

				int InsertIdx = 0;

				while(TabMenu_RecentProjects.DropDownItems[InsertIdx] != TabMenu_Recent_Separator)
				{
					TabMenu_RecentProjects.DropDownItems.RemoveAt(InsertIdx);
				}

				TabMenu_TabIdx = -1;
				for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
				{
					if(TabControl.GetTabData(Idx) == TabData)
					{
						TabMenu_TabIdx = Idx;
						break;
					}
				}

				foreach(UserSelectedProjectSettings RecentProject in Settings.RecentProjects)
				{
					ToolStripMenuItem Item = new ToolStripMenuItem(RecentProject.ToString(), null, new EventHandler((o, e) => TabMenu_OpenRecentProject_Click(RecentProject, TabMenu_TabIdx)));
					TabMenu_RecentProjects.DropDownItems.Insert(InsertIdx, Item);
					InsertIdx++;
				}

				TabMenu_RecentProjects.Visible = (Settings.RecentProjects.Count > 0);

				TabMenu_TabNames_Stream.Checked = Settings.TabLabels == TabLabels.Stream;
				TabMenu_TabNames_WorkspaceName.Checked = Settings.TabLabels == TabLabels.WorkspaceName;
				TabMenu_TabNames_WorkspaceRoot.Checked = Settings.TabLabels == TabLabels.WorkspaceRoot;
				TabMenu_TabNames_ProjectFile.Checked = Settings.TabLabels == TabLabels.ProjectFile;
				TabMenu.Show(TabControl, Location);

				TabControl.LockHover();
			}
		}

		void TabControl_OnTabReorder()
		{
			SaveTabSettings();
		}

		void TabControl_OnTabClosed(object Data)
		{
			IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)Data;
			if(CurrentTabPanel == TabPanel)
			{
				CurrentTabPanel = null;
			}
			TabPanel.Dispose();

			SaveTabSettings();
		}

		bool TabControl_OnTabClosing(object TabData)
		{
			IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabData;
			return TabPanel.CanClose();
		}

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}

			for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				((IMainWindowTabPanel)TabControl.GetTabData(Idx)).Dispose();
			}

			StopScheduleTimer();

			foreach (IssueMonitor DefaultIssueMonitor in DefaultIssueMonitors)
			{
				ReleaseIssueMonitor(DefaultIssueMonitor);
			}
			DefaultIssueMonitors.Clear();
			Debug.Assert(WorkspaceIssueMonitors.Count == 0);

			if(AutomationServer != null)
			{
				AutomationServer.Dispose();
				AutomationServer = null!;
			}

			if (ToolUpdateMonitor != null)
			{
				ToolUpdateMonitor.Dispose();
				ToolUpdateMonitor = null!;
			}

			base.Dispose(disposing);
		}

		public bool ConfirmClose()
		{
			for (int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(Idx);
				if (!TabPanel.CanClose())
				{
					return false;
				}
			}
			return true;
		}

		private void MainWindow_FormClosing(object Sender, FormClosingEventArgs EventArgs)
		{
			if(!bAllowClose && Settings.bKeepInTray)
			{
				Hide();
				EventArgs.Cancel = true;
			}
			else
			{
				if (!bAllowClose)
				{
					for (int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
					{
						IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(Idx)!;
						if (!TabPanel.CanClose())
						{
							EventArgs.Cancel = true;
							return;
						}
					}
				}

				StopScheduleTimer();
			}

			Settings.bWindowVisible = Visible;
			Settings.WindowState = WindowState.ToString();
			if(WindowState == FormWindowState.Normal)
			{
				Settings.WindowBounds = new Rectangle(Location, Size);
			}
			else
			{
				Settings.WindowBounds = RestoreBounds;
			}

			Settings.Save(Logger);
		}

		private void SetupDefaultControl()
		{
			List<StatusLine> Lines = new List<StatusLine>();

			StatusLine SummaryLine = new StatusLine();
			SummaryLine.AddText("To get started, open an existing Unreal project file on your hard drive.");
			Lines.Add(SummaryLine);

			StatusLine OpenLine = new StatusLine();
			OpenLine.AddLink("Open project...", FontStyle.Bold | FontStyle.Underline, () => { OpenNewProject(); });
			OpenLine.AddText("  |  ");
			OpenLine.AddLink("Application settings...", FontStyle.Bold | FontStyle.Underline, () => { ModifyApplicationSettings(); });
			Lines.Add(OpenLine);

			DefaultControl.Set(Lines, null, null, null);
		}

		private void CreateErrorPanel(int ReplaceTabIdx, UserSelectedProjectSettings Project, string Message)
		{
			Logger.LogError("{Error}", Message ?? "Unknown error");

			ErrorPanel ErrorPanel = new ErrorPanel(Project);
			ErrorPanel.Parent = TabPanel;
			ErrorPanel.BorderStyle = BorderStyle.FixedSingle;
			ErrorPanel.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(250)))), ((int)(((byte)(250)))), ((int)(((byte)(250)))));
			ErrorPanel.Location = new Point(0, 0);
			ErrorPanel.Dock = DockStyle.Fill;
			ErrorPanel.Hide();

			string SummaryText = String.Format("Unable to open '{0}'.", Project.ToString());

			int NewContentWidth = Math.Max(TextRenderer.MeasureText(SummaryText, ErrorPanel.Font).Width, 400);
			if(!String.IsNullOrEmpty(Message))
			{
				NewContentWidth = Math.Max(NewContentWidth, TextRenderer.MeasureText(Message, ErrorPanel.Font).Width);
			}

			ErrorPanel.SetContentWidth(NewContentWidth);

			List<StatusLine> Lines = new List<StatusLine>();

			StatusLine SummaryLine = new StatusLine();
			SummaryLine.AddText(SummaryText);
			Lines.Add(SummaryLine);

			if(!String.IsNullOrEmpty(Message))
			{
				Lines.Add(new StatusLine(){ LineHeight = 0.5f });

				foreach(string MessageLine in Message.Split('\n'))
				{
					StatusLine ErrorLine = new StatusLine();
					ErrorLine.AddText(MessageLine);
					ErrorLine.LineHeight = 0.8f;
					Lines.Add(ErrorLine);
				}
			}

			Lines.Add(new StatusLine(){ LineHeight = 0.5f });

			StatusLine ActionLine = new StatusLine();
			ActionLine.AddLink("Retry", FontStyle.Bold | FontStyle.Underline, () => { BeginInvoke(new MethodInvoker(() => { TryOpenProject(Project, TabControl.FindTabIndex(ErrorPanel)); })); });
			ActionLine.AddText(" | ");
			ActionLine.AddLink("Settings", FontStyle.Bold | FontStyle.Underline, () => { BeginInvoke(new MethodInvoker(() => { EditSelectedProject(ErrorPanel); })); });
			ActionLine.AddText(" | ");
			ActionLine.AddLink("Close", FontStyle.Bold | FontStyle.Underline, () => { BeginInvoke(new MethodInvoker(() => { TabControl.RemoveTab(TabControl.FindTabIndex(ErrorPanel)); })); });
			Lines.Add(ActionLine);

			ErrorPanel.Set(Lines, null, null, null);

			string NewProjectName = "Unknown";
			if(Project.Type == UserSelectedProjectType.Client && Project.ClientPath != null)
			{
				NewProjectName = Project.ClientPath.Substring(Project.ClientPath.LastIndexOf('/') + 1);
			}
			if(Project.Type == UserSelectedProjectType.Local && Project.LocalPath != null)
			{
				NewProjectName = Project.LocalPath.Substring(Project.LocalPath.LastIndexOfAny(new char[]{ '/', '\\' }) + 1);
			}

			string NewTabName = String.Format("Error: {0}", NewProjectName);
			if (ReplaceTabIdx == -1)
			{
				int TabIdx = TabControl.InsertTab(-1, NewTabName, ErrorPanel, ErrorPanel.TintColor);
				TabControl.SelectTab(TabIdx);
			}
			else
			{
				TabControl.InsertTab(ReplaceTabIdx + 1, NewTabName, ErrorPanel, ErrorPanel.TintColor);
				TabControl.RemoveTab(ReplaceTabIdx);
				TabControl.SelectTab(ReplaceTabIdx);
			}

			UpdateProgress();
		}

		[DllImport("user32.dll")]
		private static extern int ShowWindow(IntPtr hWnd, uint Msg);

		private const uint SW_RESTORE = 0x09;

		public void ShowAndActivate()
		{
			if (!IsDisposed)
			{
				Show();
				if (WindowState == FormWindowState.Minimized)
				{
					ShowWindow(Handle, SW_RESTORE);
				}
				Activate();

				Settings.bWindowVisible = Visible;
				Settings.Save(Logger);
			}
		}

		public bool CanPerformUpdate()
		{
			if(ContainsFocus || Form.ActiveForm == this)
			{
				return false;
			}

			for (int TabIdx = 0; TabIdx < TabControl.GetTabCount(); TabIdx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(TabIdx)!;
				if(TabPanel.IsBusy())
				{
					return false;
				}
			}

			return true;
		}

		public bool CanSyncNow()
		{
			return CurrentTabPanel != null && CurrentTabPanel.CanSyncNow();
		}

		public bool CanLaunchEditor()
		{
			return CurrentTabPanel != null && CurrentTabPanel.CanLaunchEditor();
		}

		public void SyncLatestChange()
		{
			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.SyncLatestChange();
			}
		}

		public void LaunchEditor()
		{
			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.LaunchEditor();
			}
		}

		public void ForceClose()
		{
			bAllowClose = true;
			Close();
		}

		private void MainWindow_Activated(object sender, EventArgs e)
		{
			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.Activate();
			}
		}

		private void MainWindow_Deactivate(object sender, EventArgs e)
		{
			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.Deactivate();
			}
		}

		public void SetupScheduledSync()
		{
			StopScheduleTimer();

			List<UserSelectedProjectSettings> OpenProjects = new List<UserSelectedProjectSettings>();
			for(int TabIdx = 0; TabIdx < TabControl.GetTabCount(); TabIdx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(TabIdx);
				OpenProjects.Add(TabPanel.SelectedProject);
			}

			Dictionary<UserSelectedProjectSettings, List<LatestChangeType>> ProjectToLatestChangeTypes = new Dictionary<UserSelectedProjectSettings, List<LatestChangeType>>();
			for (int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				WorkspaceControl? Workspace = TabControl.GetTabData(Idx) as WorkspaceControl;
				if (Workspace != null)
				{
					ProjectToLatestChangeTypes.Add(Workspace.SelectedProject, Workspace.GetCustomLatestChangeTypes());
				}
			}

			ScheduleWindow Schedule = new ScheduleWindow(Settings.bScheduleEnabled, Settings.ScheduleTime, Settings.ScheduleAnyOpenProject, Settings.ScheduleProjects, OpenProjects, ProjectToLatestChangeTypes);
			if(Schedule.ShowDialog() == System.Windows.Forms.DialogResult.OK)
			{
				Schedule.CopySettings(out Settings.bScheduleEnabled, out Settings.ScheduleTime, out Settings.ScheduleAnyOpenProject, out Settings.ScheduleProjects);
				Settings.Save(Logger);
			}

			StartScheduleTimer();
		}

		private void StartScheduleTimer()
		{
			StopScheduleTimer();

			if(Settings.bScheduleEnabled)
			{
				DateTime CurrentTime = DateTime.Now;
				Random Rnd = new Random();

				// add or subtract from the schedule time to distribute scheduled syncs over a little bit more time
				// this avoids everyone hitting the p4 server at exactly the same time.
				const int FudgeMinutes = 10;
				TimeSpan FudgeTime = TimeSpan.FromMinutes(Rnd.Next(FudgeMinutes * -100, FudgeMinutes * 100) / 100.0);
				DateTime NextScheduleTime = new DateTime(CurrentTime.Year, CurrentTime.Month, CurrentTime.Day, Settings.ScheduleTime.Hours, Settings.ScheduleTime.Minutes, Settings.ScheduleTime.Seconds);
				NextScheduleTime += FudgeTime;

				if (NextScheduleTime < CurrentTime)
				{
					NextScheduleTime = NextScheduleTime.AddDays(1.0);
				}

				TimeSpan IntervalToFirstTick = NextScheduleTime - CurrentTime;
				ScheduleTimer = new System.Threading.Timer(x => MainThreadSynchronizationContext.Post((o) => { if(!IsDisposed){ ScheduleTimerElapsed(); } }, null), null, IntervalToFirstTick, TimeSpan.FromDays(1));

				Logger.LogInformation("Schedule: Started ScheduleTimer for {Time} ({Time} remaining)", NextScheduleTime, IntervalToFirstTick);
			}
		}

		private void StopScheduleTimer()
		{
			if(ScheduleTimer != null)
			{
				ScheduleTimer.Dispose();
				ScheduleTimer = null;
				Logger.LogInformation("Schedule: Stopped ScheduleTimer");
			}
			StopScheduleSettledTimer();
		}

		private void ScheduleTimerElapsed()
		{
			Logger.LogInformation("Schedule: Timer Elapsed");

			// Try to open any missing tabs. 
			int NumInitialTabs = TabControl.GetTabCount();
			foreach (UserSelectedProjectSettings ScheduledProject in Settings.ScheduleProjects)
			{
				Logger.LogInformation("Schedule: Attempting to open {Project}", ScheduledProject);
				TryOpenProject(ScheduledProject, -1, OpenProjectOptions.Quiet);
			}

			// If we did open something, leave it for a while to populate with data before trying to start the sync.
			if(TabControl.GetTabCount() > NumInitialTabs)
			{
				StartScheduleSettledTimer();
			}
			else
			{
				ScheduleSettledTimerElapsed();
			}
		}

		private void StartScheduleSettledTimer()
		{
			StopScheduleSettledTimer();
			ScheduleSettledTimer = new System.Threading.Timer(x => MainThreadSynchronizationContext.Post((o) => { if(!IsDisposed){ ScheduleSettledTimerElapsed(); } }, null), null, TimeSpan.FromSeconds(20.0), TimeSpan.FromMilliseconds(-1.0));
			Logger.LogInformation("Schedule: Started ScheduleSettledTimer");
		}

		private void StopScheduleSettledTimer()
		{
			if(ScheduleSettledTimer != null)
			{
				ScheduleSettledTimer.Dispose();
				ScheduleSettledTimer = null;

				Logger.LogInformation("Schedule: Stopped ScheduleSettledTimer");
			}
		}

		private void ScheduleSettledTimerElapsed()
		{
			Logger.LogInformation("Schedule: Starting Sync");
			for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				WorkspaceControl? Workspace = TabControl.GetTabData(Idx) as WorkspaceControl;
				if(Workspace != null)
				{
					Logger.LogInformation("Schedule: Considering {File}", Workspace.SelectedFileName);
					if(Settings.ScheduleAnyOpenProject || Settings.ScheduleProjects.Any(x => x.LocalPath != null && x.LocalPath.Equals(Workspace.SelectedProject.LocalPath, StringComparison.OrdinalIgnoreCase)))
					{
						Logger.LogInformation("Schedule: Starting Sync");
						Workspace.ScheduleTimerElapsed();
					}
				}
			}
		}

		void TabControl_OnTabChanged(object? NewTabData)
		{
			if(IsHandleCreated)
			{
				SendMessage(Handle, WM_SETREDRAW, 0, 0);
			}

			SuspendLayout();

			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.Deactivate();
				CurrentTabPanel.Hide();
			}

			if(NewTabData == null)
			{
				CurrentTabPanel = null;
				Settings.LastProject = null;
				DefaultControl.Show();
			}
			else
			{
				CurrentTabPanel = (IMainWindowTabPanel)NewTabData;
				Settings.LastProject = CurrentTabPanel.SelectedProject;
				DefaultControl.Hide();
			}

			Settings.Save(Logger);

			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.Activate();
				CurrentTabPanel.Show();
			}

			ResumeLayout();

			if(IsHandleCreated)
			{
				SendMessage(Handle, WM_SETREDRAW, 1, 0);
			}

			Refresh();
		}

		public void RequestProjectChange(WorkspaceControl Workspace, UserSelectedProjectSettings Project, bool bModal)
		{
			int TabIdx = TabControl.FindTabIndex(Workspace);
			if(TabIdx != -1 && !Workspace.IsBusy() && CanFocus)
			{
				if(bModal)
				{
					TryOpenProject(Project, TabIdx);
				}
				else
				{
					TryOpenProject(Project, TabIdx, OpenProjectOptions.Quiet);
				}
			}
		}

		public void OpenNewProject()
		{
			OpenProjectInfo? OpenProjectInfo = OpenProjectWindow.ShowModal(this, null, Settings, DataFolder, CacheFolder, DefaultPerforceSettings, ServiceProvider, Logger);
			if(OpenProjectInfo != null)
			{
				int NewTabIdx = TryOpenProject(OpenProjectInfo, -1, OpenProjectOptions.None);
				if(NewTabIdx != -1)
				{
					TabControl.SelectTab(NewTabIdx);
					SaveTabSettings();
					UpdateRecentProjectsList(OpenProjectInfo.SelectedProject);
				}
			}
		}

		void UpdateRecentProjectsList(UserSelectedProjectSettings DetectedProjectSettings)
		{
			Settings.RecentProjects.RemoveAll(x => x.LocalPath != null && x.LocalPath.Equals(DetectedProjectSettings.LocalPath, StringComparison.OrdinalIgnoreCase));
			Settings.RecentProjects.Insert(0, DetectedProjectSettings);

			const int MaxRecentProjects = 10;
			if (Settings.RecentProjects.Count > MaxRecentProjects)
			{
				Settings.RecentProjects.RemoveRange(MaxRecentProjects, Settings.RecentProjects.Count - MaxRecentProjects);
			}

			Settings.Save(Logger);
		}

		public void EditSelectedProject(int TabIdx)
		{
			object TabData = TabControl.GetTabData(TabIdx);
			if(TabData is WorkspaceControl)
			{
				WorkspaceControl Workspace = (WorkspaceControl)TabData;
				EditSelectedProject(TabIdx, Workspace.SelectedProject);
			}
			else if(TabData is ErrorPanel)
			{
				ErrorPanel Error = (ErrorPanel)TabData;
				EditSelectedProject(TabIdx, Error.SelectedProject);
			}
		}

		public void EditSelectedProject(WorkspaceControl Workspace)
		{
			int TabIdx = TabControl.FindTabIndex(Workspace);
			if(TabIdx != -1)
			{
				EditSelectedProject(TabIdx, Workspace.SelectedProject);
			}
		}

		public void EditSelectedProject(ErrorPanel Panel)
		{
			int TabIdx = TabControl.FindTabIndex(Panel);
			if(TabIdx != -1)
			{
				EditSelectedProject(TabIdx, Panel.SelectedProject);
			}
		}

		public void EditSelectedProject(int TabIdx, UserSelectedProjectSettings SelectedProject)
		{
			OpenProjectInfo? OpenProjectInfo = OpenProjectWindow.ShowModal(this, SelectedProject, Settings, DataFolder, CacheFolder, DefaultPerforceSettings, ServiceProvider, Logger);
			if(OpenProjectInfo != null)
			{
				int NewTabIdx = TryOpenProject(OpenProjectInfo, TabIdx, OpenProjectOptions.None);
				if(NewTabIdx != -1)
				{
					TabControl.SelectTab(NewTabIdx);
					SaveTabSettings();
					UpdateRecentProjectsList(OpenProjectInfo.SelectedProject);
				}
			}
		}

		int TryOpenProject(UserSelectedProjectSettings Project, int ReplaceTabIdx, OpenProjectOptions Options = OpenProjectOptions.None)
		{
			ILogger<OpenProjectInfo> ProjectLogger = ServiceProvider.GetRequiredService<ILogger<OpenProjectInfo>>();

			ModalTaskFlags TaskFlags = ModalTaskFlags.None;
			if((Options & OpenProjectOptions.Quiet) != 0)
			{
				TaskFlags |= ModalTaskFlags.Quiet;
			}

			PerforceSettings PerforceSettings = Utility.OverridePerforceSettings(DefaultPerforceSettings, Project.ServerAndPort, Project.UserName);

			ModalTask<OpenProjectInfo>? SettingsTask = PerforceModalTask.Execute(this, "Opening Project", "Opening project, please wait...", PerforceSettings, (p, c) => OpenProjectWindow.DetectSettingsAsync(p, Project, Settings, ProjectLogger, c), ProjectLogger, TaskFlags);
			if (SettingsTask == null || SettingsTask.Failed)
			{
				if(SettingsTask != null) CreateErrorPanel(ReplaceTabIdx, Project, SettingsTask.Error);
				return -1;
			}

			return TryOpenProject(SettingsTask.Result, ReplaceTabIdx, Options);
		}

		int TryOpenProject(OpenProjectInfo OpenProjectInfo, int ReplaceTabIdx, OpenProjectOptions Options)
		{
			Logger.LogInformation("Trying to open project {Project}", OpenProjectInfo.ProjectInfo.ClientFileName);

			// Check that none of the other tabs already have it open
			for(int TabIdx = 0; TabIdx < TabControl.GetTabCount(); TabIdx++)
			{
				if(ReplaceTabIdx != TabIdx)
				{
					WorkspaceControl? Workspace = TabControl.GetTabData(TabIdx) as WorkspaceControl;
					if(Workspace != null)
					{
						if(Workspace.SelectedFileName == OpenProjectInfo.ProjectInfo.LocalFileName)
						{
							Logger.LogInformation("  Already open in tab {TabIdx}", TabIdx);
							if((Options & OpenProjectOptions.Quiet) == 0)
							{
								TabControl.SelectTab(TabIdx);
							}
							return TabIdx;
						}
						else if(OpenProjectInfo.ProjectInfo.LocalFileName.IsUnderDirectory(Workspace.BranchDirectoryName))
						{
							if((Options & OpenProjectOptions.Quiet) == 0 && MessageBox.Show(String.Format("{0} is already open under {1}.\n\nWould you like to close it?", Workspace.SelectedFileName.GetFileNameWithoutExtension(), Workspace.BranchDirectoryName, OpenProjectInfo.ProjectInfo.LocalFileName.GetFileNameWithoutExtension()), "Branch already open", MessageBoxButtons.YesNo) == System.Windows.Forms.DialogResult.Yes)
							{
								Logger.LogInformation("  Another project already open in this workspace, tab {TabIdx}. Replacing.", TabIdx);
								TabControl.RemoveTab(TabIdx);
							}
							else
							{
								Logger.LogInformation("  Another project already open in this workspace, tab {TabIdx}. Aborting.", TabIdx);
								return -1;
							}
						}
					}
				}
			}

			// Hide the default control if it's visible
			DefaultControl.Hide();

			// Remove the current tab. We need to ensure the workspace has been shut down before creating a new one with the same log files, etc...
			if(ReplaceTabIdx != -1)
			{
				WorkspaceControl? OldWorkspace = TabControl.GetTabData(ReplaceTabIdx) as WorkspaceControl;
				if(OldWorkspace != null)
				{
					OldWorkspace.Hide();
					TabControl.SetTabData(ReplaceTabIdx, new ErrorPanel(OpenProjectInfo.SelectedProject));
					OldWorkspace.Dispose();
				}
			}

			// Now that we have the project settings, we can construct the tab
			WorkspaceControl NewWorkspace = new WorkspaceControl(this, DataFolder, ApiUrl, OpenProjectInfo, ServiceProvider, Settings, OIDCTokenManager);
			
			NewWorkspace.Parent = TabPanel;
			NewWorkspace.Dock = DockStyle.Fill;
			NewWorkspace.Hide();

			// Add the tab
			string NewTabName = GetTabName(NewWorkspace);
			if(ReplaceTabIdx == -1)
			{
				int NewTabIdx = TabControl.InsertTab(-1, NewTabName, NewWorkspace, NewWorkspace.TintColor);
				Logger.LogInformation("  Inserted tab {TabIdx}", NewTabIdx);
				return NewTabIdx;
			}
			else
			{
				Logger.LogInformation("  Replacing tab {TabIdx}", ReplaceTabIdx);
				TabControl.InsertTab(ReplaceTabIdx + 1, NewTabName, NewWorkspace, NewWorkspace.TintColor);
				TabControl.RemoveTab(ReplaceTabIdx);
				return ReplaceTabIdx;
			}
		}

		public void StreamChanged(WorkspaceControl Workspace)
		{
			MainThreadSynchronizationContext.Post((o) => { if(!IsDisposed) { StreamChangedCallback(Workspace); } }, null);
		}

		public void StreamChangedCallback(WorkspaceControl Workspace)
		{
			if(ChangingWorkspacesRefCount == 0)
			{
				ChangingWorkspacesRefCount++;

				for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
				{
					if(TabControl.GetTabData(Idx) == Workspace)
					{
						UserSelectedProjectSettings Project = Workspace.SelectedProject;
						if(TryOpenProject(Project, Idx) == -1)
						{
							TabControl.RemoveTab(Idx);
						}
						break;
					}
				}

				ChangingWorkspacesRefCount--;
			}
		}

		void SaveTabSettings()
		{
			Settings.OpenProjects.Clear();
			for(int TabIdx = 0; TabIdx < TabControl.GetTabCount(); TabIdx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(TabIdx)!;
				Settings.OpenProjects.Add(TabPanel.SelectedProject);
			}
			Settings.Save(Logger);
		}

		void TabControl_OnNewTabClick(Point Location, MouseButtons Buttons)
		{
			if(Buttons == MouseButtons.Left)
			{
				OpenNewProject();
			}
		}

		string GetTabName(WorkspaceControl Workspace)
		{
			string TabName = "";
			switch (Settings.TabLabels)
			{
				case TabLabels.Stream:
					TabName = Workspace.StreamName ?? TabName;
					break;
				case TabLabels.ProjectFile:
					TabName = Workspace.SelectedFileName.FullName;
					break;
				case TabLabels.WorkspaceName:
					TabName = Workspace.ClientName;
					break;
				case TabLabels.WorkspaceRoot:
					TabName = Workspace.BranchDirectoryName.FullName;
					break;
				default:
					break;
			}

			// if this failes, return something sensible to avoid blank tabs
			if (string.IsNullOrEmpty(TabName))
			{
				Logger.LogInformation("No TabName for {ClientName} for setting {TabSetting}. Defaulting to client name", Workspace.ClientName, Settings.TabLabels.ToString());
				TabName = Workspace.ClientName;
			}

			return TabName;
		}

		public void SetTabNames(TabLabels NewTabNames)
		{
			if(Settings.TabLabels != NewTabNames)
			{
				Settings.TabLabels = NewTabNames;
				Settings.Save(Logger);

				for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
				{
					WorkspaceControl? Workspace = TabControl.GetTabData(Idx) as WorkspaceControl;
					if(Workspace != null)
					{
						TabControl.SetTabName(Idx, GetTabName(Workspace));
					}
				}
			}
		}

		private void TabMenu_OpenProject_Click(object sender, EventArgs e)
		{
			EditSelectedProject(TabMenu_TabIdx);
		}

		private void TabMenu_OpenRecentProject_Click(UserSelectedProjectSettings RecentProject, int TabIdx)
		{
			TryOpenProject(RecentProject, TabIdx);
			SaveTabSettings();
		}

		private void TabMenu_TabNames_Stream_Click(object sender, EventArgs e)
		{
			SetTabNames(TabLabels.Stream);
		}

		private void TabMenu_TabNames_WorkspaceName_Click(object sender, EventArgs e)
		{
			SetTabNames(TabLabels.WorkspaceName);
		}

		private void TabMenu_TabNames_WorkspaceRoot_Click(object sender, EventArgs e)
		{
			SetTabNames(TabLabels.WorkspaceRoot);
		}

		private void TabMenu_TabNames_ProjectFile_Click(object sender, EventArgs e)
		{
			SetTabNames(TabLabels.ProjectFile);
		}

		private void TabMenu_RecentProjects_ClearList_Click(object sender, EventArgs e)
		{
			Settings.RecentProjects.Clear();
			Settings.Save(Logger);
		}

		private void TabMenu_Closed(object sender, ToolStripDropDownClosedEventArgs e)
		{
			TabControl.UnlockHover();
		}

		private void RecentMenu_ClearList_Click(object sender, EventArgs e)
		{
			Settings.RecentProjects.Clear();
			Settings.Save(Logger);
		}

		public void UpdateProgress()
		{
			TaskbarState State = TaskbarState.NoProgress;
			float Progress = -1.0f;

			for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(Idx)!;

				Tuple<TaskbarState, float> DesiredTaskbarState = TabPanel.DesiredTaskbarState;
				if(DesiredTaskbarState.Item1 == TaskbarState.Error)
				{
					State = TaskbarState.Error;
					TabControl.SetHighlight(Idx, Tuple.Create(Color.FromArgb(204, 64, 64), 1.0f));
				}
				else if(DesiredTaskbarState.Item1 == TaskbarState.Paused && State != TaskbarState.Error)
				{
					State = TaskbarState.Paused;
					TabControl.SetHighlight(Idx, Tuple.Create(Color.FromArgb(255, 242, 0), 1.0f));
				}
				else if(DesiredTaskbarState.Item1 == TaskbarState.Normal && State != TaskbarState.Error && State != TaskbarState.Paused)
				{
					State = TaskbarState.Normal;
					Progress = Math.Max(Progress, DesiredTaskbarState.Item2);
					TabControl.SetHighlight(Idx, Tuple.Create(Color.FromArgb(28, 180, 64), DesiredTaskbarState.Item2));
				}
				else
				{
					TabControl.SetHighlight(Idx, null);
				}
			}

			if(IsHandleCreated)
			{
				if(State == TaskbarState.Normal)
				{
					Taskbar.SetState(Handle, TaskbarState.Normal);
					Taskbar.SetProgress(Handle, (ulong)(Progress * 1000.0f), 1000);
				}
				else
				{
					Taskbar.SetState(Handle, State);
				}
			}
		}

		public void UpdateTintColors()
		{
			for (int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(Idx)!;
				TabControl.SetTint(Idx, TabPanel.TintColor);
			}
		}

		public void ModifyApplicationSettings()
		{
			bool? bRelaunchPreview = ApplicationSettingsWindow.ShowModal(this, DefaultPerforceSettings, bPreview, OriginalExecutableFileName, Settings, ToolUpdateMonitor, ServiceProvider.GetRequiredService<ILogger<ApplicationSettingsWindow>>());
			if(bRelaunchPreview.HasValue)
			{
				UpdateMonitor.TriggerUpdate(UpdateType.UserInitiated, bRelaunchPreview);
			}

			for (int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(Idx)!;
				TabPanel.UpdateSettings();
			}
		}

		private void MainWindow_Load(object sender, EventArgs e)
		{
			if(Settings.WindowBounds != null)
			{
				Rectangle WindowBounds = Settings.WindowBounds.Value;
				if(WindowBounds.Width > MinimumSize.Width && WindowBounds.Height > MinimumSize.Height)
				{
					foreach (Screen Screen in Screen.AllScreens)
					{
						if(WindowBounds.IntersectsWith(Screen.Bounds))
						{
							Location = Settings.WindowBounds.Value.Location;
							Size = Settings.WindowBounds.Value.Size;
							break;
						}
					}
				}
			}

			FormWindowState NewWindowState;
			if (Enum.TryParse(Settings.WindowState, true, out NewWindowState))
			{
				WindowState = NewWindowState;
			}
		}

		bool ShowNotificationForIssue(IssueData Issue)
		{
			return Issue.Projects.Any(x => ShowNotificationsForProject(x));
		}

		bool ShowNotificationsForProject(string Project)
		{
			return String.IsNullOrEmpty(Project) || Settings.NotifyProjects.Count == 0 || Settings.NotifyProjects.Any(x => x.Equals(Project, StringComparison.OrdinalIgnoreCase));
		}

		public void UpdateAlertWindows()
		{
			if (!DeploymentSettings.EnableAlerts)
			{
				return;
			}

			HashSet<IssueData> AllIssues = new HashSet<IssueData>();
			foreach(IssueMonitor IssueMonitor in WorkspaceIssueMonitors.Select(x => x.IssueMonitor))
			{
				List<IssueData> Issues = IssueMonitor.GetIssues();
				foreach(IssueData Issue in Issues)
				{
					IssueAlertReason Reason = 0;
					if(Issue.FixChange == 0 && !Issue.ResolvedAt.HasValue)
					{
						if(Issue.Owner == null)
						{
							if(Issue.bNotify)
							{
								Reason |= IssueAlertReason.Normal;
							}
							if(ShowNotificationForIssue(Issue) && Settings.NotifyUnassignedMinutes >= 0 && Issue.RetrievedAt - Issue.CreatedAt >= TimeSpan.FromMinutes(Settings.NotifyUnassignedMinutes))
							{
								Reason |= IssueAlertReason.UnassignedTimer;
							}
						}
						else if(!Issue.AcknowledgedAt.HasValue)
						{
							if(String.Compare(Issue.Owner, IssueMonitor.UserName, StringComparison.OrdinalIgnoreCase) == 0)
							{
								Reason |= IssueAlertReason.Owner;
							}
							else if(ShowNotificationForIssue(Issue) && Settings.NotifyUnacknowledgedMinutes >= 0 && Issue.RetrievedAt - Issue.CreatedAt >= TimeSpan.FromMinutes(Settings.NotifyUnacknowledgedMinutes))
							{
								Reason |= IssueAlertReason.UnacknowledgedTimer;
							}
						}
						if(ShowNotificationForIssue(Issue) && Settings.NotifyUnresolvedMinutes >= 0 && Issue.RetrievedAt - Issue.CreatedAt >= TimeSpan.FromMinutes(Settings.NotifyUnresolvedMinutes))
						{
							Reason |= IssueAlertReason.UnresolvedTimer;
						}

						IssueAlertReason PrevReason;
						if(IssueMonitor.IssueIdToAlertReason.TryGetValue(Issue.Id, out PrevReason))
						{
							Reason &= ~PrevReason;
						}
					}

					IssueAlertWindow AlertWindow = AlertWindows.FirstOrDefault(x => x.IssueMonitor == IssueMonitor && x.Issue.Id == Issue.Id);
					if(AlertWindow == null)
					{
						if(Reason != 0)
						{
							ShowAlertWindow(IssueMonitor, Issue, Reason);
						}
					}
					else
					{
						if(Reason != 0)
						{
							AlertWindow.SetIssue(Issue, Reason);
						}
						else
						{
							CloseAlertWindow(AlertWindow);
						}
					}
				}
				AllIssues.UnionWith(Issues);
			}

			// Close any alert windows which don't have an active issues
			for(int Idx = 0; Idx < AlertWindows.Count; Idx++)
			{
				IssueAlertWindow AlertWindow = AlertWindows[Idx];
				if(!AllIssues.Contains(AlertWindow.Issue))
				{
					AlertWindow.IssueMonitor.IssueIdToAlertReason.Remove(AlertWindow.Issue.Id);
					CloseAlertWindow(AlertWindow);
					Idx--;
				}
			}
		}

		void IssueMonitor_OnUpdateAsync()
		{
			MainThreadSynchronizationContext.Post((o) => IssueMonitor_OnUpdate(), null);
		}

		void IssueMonitor_OnUpdate()
		{
			UpdateAlertWindows();
		}

		void ShowAlertWindow(IssueMonitor IssueMonitor, IssueData Issue, IssueAlertReason Reason)
		{
			IssueAlertWindow Alert = new IssueAlertWindow(IssueMonitor, Issue, Reason);
			Alert.AcceptBtn.Click += (s, e) => AcceptIssue(Alert);
			Alert.DeclineBtn.Click += (s, e) => DeclineIssue(Alert);
			Alert.DetailsBtn.Click += (s, e) => ShowIssueDetails(Alert);

			SetAlertWindowPositions();
			AlertWindows.Add(Alert);
			SetAlertWindowPosition(AlertWindows.Count - 1);

			Alert.Show(this);

			UpdateAlertPositionsTimer.Enabled = true;
		}

		void AcceptIssue(IssueAlertWindow Alert)
		{
			IssueData Issue = Alert.Issue;

			IssueAlertReason Reason;
			Alert.IssueMonitor.IssueIdToAlertReason.TryGetValue(Issue.Id, out Reason);
			Alert.IssueMonitor.IssueIdToAlertReason[Issue.Id] = Reason | Alert.Reason;

			IssueUpdateData Update = new IssueUpdateData();
			Update.Id = Issue.Id;
			Update.Owner = Alert.IssueMonitor.UserName;
			Update.NominatedBy = null;
			Update.Acknowledged = true;
			Alert.IssueMonitor.PostUpdate(Update);

			CloseAlertWindow(Alert);
		}

		void DeclineIssue(IssueAlertWindow Alert)
		{
			IssueData Issue = Alert.Issue;

			IssueAlertReason Reason;
			Alert.IssueMonitor.IssueIdToAlertReason.TryGetValue(Issue.Id, out Reason);
			Alert.IssueMonitor.IssueIdToAlertReason[Issue.Id] = Reason | Alert.Reason;

			CloseAlertWindow(Alert);
		}

		void ShowIssueDetails(IssueAlertWindow Alert)
		{
			for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				WorkspaceControl? Workspace = TabControl.GetTabData(Idx) as WorkspaceControl;
				if(Workspace != null && Workspace.GetIssueMonitor() == Alert.IssueMonitor)
				{
					Workspace.ShowIssueDetails(Alert.Issue);
					break;
				}
			}
		}

		void CloseAlertWindow(IssueAlertWindow Alert)
		{
			IssueData Issue = Alert.Issue;

			Alert.Close();
			Alert.Dispose();

			AlertWindows.Remove(Alert);

			for(int Idx = 0; Idx < AlertWindows.Count; Idx++)
			{
				SetAlertWindowPosition(Idx);
			}

			if(AlertWindows.Count == 0)
			{
				UpdateAlertPositionsTimer.Enabled = false;
			}
		}

		private void SetAlertWindowPosition(int Idx)
		{
			AlertWindows[Idx].Location = new Point(PrimaryWorkArea.Right - 40 - AlertWindows[Idx].Size.Width, PrimaryWorkArea.Height - 40 - (Idx + 1) * (AlertWindows[Idx].Size.Height + 15));
		}

		private void SetAlertWindowPositions()
		{
			if (Screen.PrimaryScreen != null)
			{
				Rectangle NewPrimaryWorkArea = Screen.PrimaryScreen.WorkingArea;
				if (NewPrimaryWorkArea != PrimaryWorkArea)
				{
					PrimaryWorkArea = NewPrimaryWorkArea;
					for (int Idx = 0; Idx < AlertWindows.Count; Idx++)
					{
						SetAlertWindowPosition(Idx);
					}
				}
			}
		}

		private void UpdateAlertPositionsTimer_Tick(object sender, EventArgs e)
		{
			SetAlertWindowPositions();
		}

		public IssueMonitor CreateIssueMonitor(string? ApiUrl, string UserName)
		{
			WorkspaceIssueMonitor WorkspaceIssueMonitor = WorkspaceIssueMonitors.FirstOrDefault(x => String.Compare(x.IssueMonitor.ApiUrl, ApiUrl, StringComparison.OrdinalIgnoreCase) == 0 && String.Compare(x.IssueMonitor.UserName, UserName, StringComparison.OrdinalIgnoreCase) == 0);
			if (WorkspaceIssueMonitor == null)
			{
				string ServerId = ApiUrl != null ? Regex.Replace(ApiUrl, @"^.*://", "") : "noserver";
				ServerId = Regex.Replace(ServerId, "[^a-zA-Z.]", "+");

				FileReference LogFileName = FileReference.Combine(DataFolder, String.Format("IssueMonitor-{0}-{1}.log", ServerId, UserName));

				IssueMonitor IssueMonitor = new IssueMonitor(ApiUrl, UserName, TimeSpan.FromSeconds(60.0), ServiceProvider);
				IssueMonitor.OnIssuesChanged += IssueMonitor_OnUpdateAsync;

				WorkspaceIssueMonitor = new WorkspaceIssueMonitor(IssueMonitor);
				WorkspaceIssueMonitors.Add(WorkspaceIssueMonitor);
			}

			WorkspaceIssueMonitor.RefCount++;

			if (WorkspaceIssueMonitor.RefCount == 1 && bAllowCreatingHandle)
			{
				WorkspaceIssueMonitor.IssueMonitor.Start();
			}

			return WorkspaceIssueMonitor.IssueMonitor;
		}

		public void ReleaseIssueMonitor(IssueMonitor IssueMonitor)
		{
			int Index = WorkspaceIssueMonitors.FindIndex(x => x.IssueMonitor == IssueMonitor);
			if(Index != -1)
			{
				WorkspaceIssueMonitor WorkspaceIssueMonitor = WorkspaceIssueMonitors[Index];
				WorkspaceIssueMonitor.RefCount--;

				if (WorkspaceIssueMonitor.RefCount == 0)
				{
					for (int Idx = AlertWindows.Count - 1; Idx >= 0; Idx--)
					{
						IssueAlertWindow AlertWindow = AlertWindows[Idx];
						if (AlertWindow.IssueMonitor == IssueMonitor)
						{
							CloseAlertWindow(AlertWindow);
						}
					}
					IssueMonitor.OnIssuesChanged -= IssueMonitor_OnUpdateAsync;
					IssueMonitor.Release();

					WorkspaceIssueMonitors.RemoveAt(Index);
				}
			}
		}
	}
}
