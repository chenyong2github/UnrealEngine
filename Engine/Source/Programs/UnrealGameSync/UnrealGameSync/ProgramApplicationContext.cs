// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using UnrealGameSync.Forms;

#nullable enable

namespace UnrealGameSync
{
	class ProgramApplicationContext : ApplicationContext
	{
		SynchronizationContext MainThreadSynchronizationContext;

		IPerforceSettings DefaultPerforceSettings;
		UpdateMonitor UpdateMonitor;
		string? ApiUrl;
		DirectoryReference DataFolder;
		DirectoryReference CacheFolder;
		bool bRestoreState;
		string? UpdateSpawn;
		bool bUnstable;
		bool bIsClosing;
		string? Uri;

		IServiceProvider ServiceProvider;
		ILogger Logger;
		UserSettings Settings;
		ActivationListener ActivationListener;

		Container Components = new Container();
		NotifyIcon NotifyIcon;
		ContextMenuStrip NotifyMenu;
		ToolStripMenuItem NotifyMenu_OpenUnrealGameSync;
		ToolStripSeparator NotifyMenu_OpenUnrealGameSync_Separator;
		ToolStripMenuItem NotifyMenu_SyncNow;
		ToolStripMenuItem NotifyMenu_LaunchEditor;
		ToolStripSeparator NotifyMenu_ExitSeparator;
		ToolStripMenuItem NotifyMenu_Exit;

		CancellationTokenSource StartupCancellationSource = new CancellationTokenSource();
		Task StartupTask;
		ModalTaskWindow? StartupWindow;
		MainWindow? MainWindowInstance;

		OIDCTokenManager? OIDCTokenManager;

		public ProgramApplicationContext(IPerforceSettings DefaultPerforceSettings, UpdateMonitor UpdateMonitor, string? ApiUrl, DirectoryReference DataFolder, EventWaitHandle ActivateEvent, bool bRestoreState, string? UpdateSpawn, string? ProjectFileName, bool bUnstable, IServiceProvider ServiceProvider, string? Uri)
		{
			this.DefaultPerforceSettings = DefaultPerforceSettings;
			this.UpdateMonitor = UpdateMonitor;
			this.ApiUrl = ApiUrl;
			this.DataFolder = DataFolder;
			this.CacheFolder = DirectoryReference.Combine(DataFolder, "Cache");
			this.bRestoreState = bRestoreState;
			this.UpdateSpawn = UpdateSpawn;
			this.bUnstable = bUnstable;
			this.ServiceProvider = ServiceProvider;
			this.Logger = ServiceProvider.GetRequiredService<ILogger<ProgramApplicationContext>>();
			this.Uri = Uri;

			// Create the directories
			DirectoryReference.CreateDirectory(DataFolder);
			DirectoryReference.CreateDirectory(CacheFolder);

			// Make sure a synchronization context is set. We spawn a bunch of threads (eg. UpdateMonitor) at startup, and need to make sure we can post messages 
			// back to the main thread at any time.
			if(SynchronizationContext.Current == null)
			{
				SynchronizationContext.SetSynchronizationContext(new WindowsFormsSynchronizationContext());
			}

			// Capture the main thread's synchronization context for callbacks
			MainThreadSynchronizationContext = SynchronizationContext.Current!;

			// Read the user's settings
			Settings = UserSettings.Create(DataFolder, ServiceProvider.GetRequiredService<ILogger<UserSettings>>());
			if(!String.IsNullOrEmpty(ProjectFileName))
			{
				string FullProjectFileName = Path.GetFullPath(ProjectFileName);
				if(!Settings.OpenProjects.Any(x => x.LocalPath != null && String.Compare(x.LocalPath, FullProjectFileName, StringComparison.InvariantCultureIgnoreCase) == 0))
				{
					Settings.OpenProjects.Add(new UserSelectedProjectSettings(null, null, UserSelectedProjectType.Local, null, FullProjectFileName));
				}
			}

			// Update the settings to the latest version
			if(Settings.Version < UserSettingsVersion.Latest)
			{
				// Clear out the server settings for anything using the default server
				if(Settings.Version < UserSettingsVersion.DefaultServerSettings)
				{
					Logger.LogInformation("Clearing project settings for default server");
					for (int Idx = 0; Idx < Settings.OpenProjects.Count; Idx++)
					{
						Settings.OpenProjects[Idx] = UpgradeSelectedProjectSettings(Settings.OpenProjects[Idx]);
					}
					for (int Idx = 0; Idx < Settings.RecentProjects.Count; Idx++)
					{
						Settings.RecentProjects[Idx] = UpgradeSelectedProjectSettings(Settings.RecentProjects[Idx]);
					}
				}

				// Save the new settings
				Settings.Version = UserSettingsVersion.Latest;
				Settings.Save();
			}

			// Register the update listener
			UpdateMonitor.OnUpdateAvailable += OnUpdateAvailableCallback;

			// Create the activation listener
			ActivationListener = new ActivationListener(ActivateEvent);
			ActivationListener.Start();
			ActivationListener.OnActivate += OnActivationListenerAsyncCallback;

			// Create the notification menu items
			NotifyMenu_OpenUnrealGameSync = new ToolStripMenuItem();
			NotifyMenu_OpenUnrealGameSync.Name = nameof(NotifyMenu_OpenUnrealGameSync);
			NotifyMenu_OpenUnrealGameSync.Size = new Size(196, 22);
			NotifyMenu_OpenUnrealGameSync.Text = "Open UnrealGameSync";
			NotifyMenu_OpenUnrealGameSync.Click += new EventHandler(NotifyMenu_OpenUnrealGameSync_Click);
			NotifyMenu_OpenUnrealGameSync.Font = new Font(NotifyMenu_OpenUnrealGameSync.Font, FontStyle.Bold);

			NotifyMenu_OpenUnrealGameSync_Separator = new ToolStripSeparator();
			NotifyMenu_OpenUnrealGameSync_Separator.Name = nameof(NotifyMenu_OpenUnrealGameSync_Separator);
			NotifyMenu_OpenUnrealGameSync_Separator.Size = new Size(193, 6);

			NotifyMenu_SyncNow = new ToolStripMenuItem();
			NotifyMenu_SyncNow.Name = nameof(NotifyMenu_SyncNow);
			NotifyMenu_SyncNow.Size = new Size(196, 22);
			NotifyMenu_SyncNow.Text = "Sync Now";
			NotifyMenu_SyncNow.Click += new EventHandler(NotifyMenu_SyncNow_Click);

			NotifyMenu_LaunchEditor = new ToolStripMenuItem();
			NotifyMenu_LaunchEditor.Name = nameof(NotifyMenu_LaunchEditor);
			NotifyMenu_LaunchEditor.Size = new Size(196, 22);
			NotifyMenu_LaunchEditor.Text = "Launch Editor";
			NotifyMenu_LaunchEditor.Click += new EventHandler(NotifyMenu_LaunchEditor_Click);

			NotifyMenu_ExitSeparator = new ToolStripSeparator();
			NotifyMenu_ExitSeparator.Name = nameof(NotifyMenu_ExitSeparator);
			NotifyMenu_ExitSeparator.Size = new Size(193, 6);

			NotifyMenu_Exit = new ToolStripMenuItem();
			NotifyMenu_Exit.Name = nameof(NotifyMenu_Exit);
			NotifyMenu_Exit.Size = new Size(196, 22);
			NotifyMenu_Exit.Text = "Exit";
			NotifyMenu_Exit.Click += new EventHandler(NotifyMenu_Exit_Click);

			// Create the notification menu
			NotifyMenu = new ContextMenuStrip(Components);
			NotifyMenu.Name = nameof(NotifyMenu);
			NotifyMenu.Size = new System.Drawing.Size(197, 104);
			NotifyMenu.SuspendLayout();
			NotifyMenu.Items.Add(NotifyMenu_OpenUnrealGameSync);
			NotifyMenu.Items.Add(NotifyMenu_OpenUnrealGameSync_Separator);
			NotifyMenu.Items.Add(NotifyMenu_SyncNow);
			NotifyMenu.Items.Add(NotifyMenu_LaunchEditor);
			NotifyMenu.Items.Add(NotifyMenu_ExitSeparator);
			NotifyMenu.Items.Add(NotifyMenu_Exit);
			NotifyMenu.ResumeLayout(false);

			// Create the notification icon
			NotifyIcon = new NotifyIcon(Components);
			NotifyIcon.ContextMenuStrip = NotifyMenu;
			NotifyIcon.Icon = Properties.Resources.Icon;
			NotifyIcon.Text = "UnrealGameSync";
			NotifyIcon.Visible = true;
			NotifyIcon.DoubleClick += new EventHandler(NotifyIcon_DoubleClick);
			NotifyIcon.MouseDown += new MouseEventHandler(NotifyIcon_MouseDown);

			// Create the startup tasks
			List<(UserSelectedProjectSettings, ModalTask<OpenProjectInfo>)> StartupTasks = new List<(UserSelectedProjectSettings, ModalTask<OpenProjectInfo>)>();
			foreach (UserSelectedProjectSettings ProjectSettings in Settings.OpenProjects)
			{
				ILogger<OpenProjectInfo> Logger = ServiceProvider.GetRequiredService<ILogger<OpenProjectInfo>>();
				Task<OpenProjectInfo> StartupTask = Task.Run(() => OpenProjectInfo.CreateAsync(DefaultPerforceSettings, ProjectSettings, Settings, Logger, StartupCancellationSource.Token), StartupCancellationSource.Token);
				StartupTasks.Add((ProjectSettings, new ModalTask<OpenProjectInfo>(StartupTask)));
			}
			StartupTask = Task.WhenAll(StartupTasks.Select(x => x.Item2.Task));

			StartupWindow = new ModalTaskWindow("Opening Projects", "Opening projects, please wait...", FormStartPosition.CenterScreen, StartupTask, StartupCancellationSource);
			Components.Add(StartupWindow);

			if(bRestoreState)
			{
				if(Settings.bWindowVisible)
				{
					StartupWindow.Show();
				}
			}
			else
			{
				StartupWindow.Show();
				StartupWindow.Activate();
			}
			StartupWindow.FormClosed += (s, e) => OnStartupComplete(StartupTasks);
		}

		private UserSelectedProjectSettings UpgradeSelectedProjectSettings(UserSelectedProjectSettings Project)
		{
			if (Project.ServerAndPort == null || String.Compare(Project.ServerAndPort, DefaultPerforceSettings.ServerAndPort, StringComparison.OrdinalIgnoreCase) == 0)
			{
				if (Project.UserName == null || String.Compare(Project.UserName, DefaultPerforceSettings.UserName, StringComparison.OrdinalIgnoreCase) == 0)
				{
					Project = new UserSelectedProjectSettings(null, null, Project.Type, Project.ClientPath, Project.LocalPath);
				}
			}
			return Project;
		}

		private void OnStartupComplete(List<(UserSelectedProjectSettings, ModalTask<OpenProjectInfo>)> StartupTasks)
		{
			// Close the startup window
			bool bVisible = StartupWindow!.Visible;
			StartupWindow = null;

			// Clear out the cache folder
			Utility.ClearPrintCache(CacheFolder);

			OIDCTokenManager = OIDCTokenManager.CreateFromConfigFile(Settings, StartupTasks.Where(x => x.Item2.Succeeded).Select(x => x.Item2.Result).ToList());
			// Verify that none of the projects we are opening needs a OIDC login, if they do prompt for the login
			if (OIDCTokenManager?.HasUnfinishedLogin() ?? false)
			{
				OIDCLoginWindow LoginDialog = new OIDCLoginWindow(OIDCTokenManager);
				LoginDialog.ShowDialog();
			}

			// Get the application path
			string? OriginalExe = UpdateSpawn;
			if (OriginalExe == null)
			{
				OriginalExe = Path.ChangeExtension(Assembly.GetExecutingAssembly().Location, ".exe");
			}

			// Create the main window
			MainWindowInstance = new MainWindow(UpdateMonitor, ApiUrl, DataFolder, CacheFolder, bRestoreState, OriginalExe, bUnstable, StartupTasks, DefaultPerforceSettings, ServiceProvider, Settings, Uri, OIDCTokenManager);
			if(bVisible)
			{
				MainWindowInstance.Show();
				if(!bRestoreState)
				{
					MainWindowInstance.Activate();
				}
			}
			MainWindowInstance.FormClosed += MainWindowInstance_FormClosed;
		}

		private void MainWindowInstance_FormClosed(object? sender, FormClosedEventArgs e)
		{
			ExitThread();
		}

		private void OnActivationListenerCallback()
		{
			if(MainWindowInstance != null && !MainWindowInstance.IsDisposed)
			{
				MainWindowInstance.ShowAndActivate();
			}
		}

		private void OnActivationListenerAsyncCallback()
		{
			MainThreadSynchronizationContext.Post((o) => OnActivationListenerCallback(), null);
		}

		private void OnUpdateAvailable(UpdateType Type)
		{
			if(MainWindowInstance != null && !bIsClosing)
			{
				if(Type == UpdateType.UserInitiated || MainWindowInstance.CanPerformUpdate())
				{
					bIsClosing = true;
					MainWindowInstance.ForceClose();
					MainWindowInstance = null;
				}
			}
		}

		private void OnUpdateAvailableCallback(UpdateType Type)
		{ 
			MainThreadSynchronizationContext.Post((o) => OnUpdateAvailable(Type), null);
		}

		protected override void Dispose(bool bDisposing)
		{
			base.Dispose(bDisposing);

			if(UpdateMonitor != null)
			{
				UpdateMonitor.Dispose();
				UpdateMonitor = null!;
			}

			if(ActivationListener != null)
			{
				ActivationListener.OnActivate -= OnActivationListenerAsyncCallback;
				ActivationListener.Stop();
				ActivationListener.Dispose();
				ActivationListener = null!;
			}

			if (Components != null)
			{
				Components.Dispose();
				Components = null!;
			}

			if (NotifyIcon != null)
			{
				NotifyIcon.Dispose();
				NotifyIcon = null!;
			}

			if(MainWindowInstance != null)
			{
				MainWindowInstance.ForceClose();
				MainWindowInstance = null!;
			}

			if(StartupWindow != null)
			{
				StartupWindow.Close();
				StartupWindow = null;
			}

			if (StartupCancellationSource != null)
			{
				StartupCancellationSource.Dispose();
				StartupCancellationSource = null!;
			}
		}

		private void NotifyIcon_MouseDown(object? sender, MouseEventArgs e)
		{
			// Have to set up this stuff here, because the menu is laid out before Opening() is called on it after mouse-up.
			bool bCanSyncNow = MainWindowInstance != null && MainWindowInstance.CanSyncNow();
			bool bCanLaunchEditor = MainWindowInstance != null && MainWindowInstance.CanLaunchEditor();
			NotifyMenu_SyncNow.Visible = bCanSyncNow;
			NotifyMenu_LaunchEditor.Visible = bCanLaunchEditor;
			NotifyMenu_ExitSeparator.Visible = bCanSyncNow || bCanLaunchEditor;

			// Show the startup window, if not already visible
			if(StartupWindow != null)
			{
				StartupWindow.Show();
			}
		}

		private void NotifyIcon_DoubleClick(object? sender, EventArgs e)
		{
			if(MainWindowInstance != null)
			{
				MainWindowInstance.ShowAndActivate();
			}
		}

		private void NotifyMenu_OpenUnrealGameSync_Click(object? sender, EventArgs e)
		{
			if(StartupWindow != null)
			{
				StartupWindow.ShowAndActivate();
			}
			if(MainWindowInstance != null)
			{
				MainWindowInstance.ShowAndActivate();
			}
		}

		private void NotifyMenu_SyncNow_Click(object? sender, EventArgs e)
		{
			if(MainWindowInstance != null)
			{
				MainWindowInstance.SyncLatestChange();
			}
		}

		private void NotifyMenu_LaunchEditor_Click(object? sender, EventArgs e)
		{
			if(MainWindowInstance != null)
			{
				MainWindowInstance.LaunchEditor();
			}
		}

		private void NotifyMenu_Exit_Click(object? sender, EventArgs e)
		{
			if (MainWindowInstance != null && !MainWindowInstance.ConfirmClose())
			{
				return;
			}

			if (StartupWindow != null)
			{
				StartupWindow.Close();
				StartupWindow = null;
			}

			if(MainWindowInstance != null)
			{
				MainWindowInstance.ForceClose();
				MainWindowInstance = null;
			}

			ExitThread();
		}

		protected override void ExitThreadCore()
		{
			base.ExitThreadCore();

			if(NotifyIcon != null)
			{
				NotifyIcon.Visible = false;
			}
		}
	}
}
