// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	public sealed class Workspace : IDisposable
	{
		public IPerforceSettings PerforceSettings { get; }
		public UserWorkspaceState State { get; }
		public ProjectInfo Project { get; }
		public SynchronizationContext SynchronizationContext { get; }
		ILogger Logger;

		bool bSyncing => CurrentUpdate != null;

		WorkspaceUpdate? CurrentUpdate;

		public event Action<WorkspaceUpdateContext, WorkspaceUpdateResult, string>? OnUpdateComplete;

		IAsyncDisposer AsyncDisposer;

		public Workspace(IPerforceSettings InPerfoceSettings, ProjectInfo InProject, UserWorkspaceState InState, ConfigFile ProjectConfigFile, IReadOnlyList<string>? ProjectStreamFilter, ILogger Logger, IServiceProvider ServiceProvider)
		{
			PerforceSettings = InPerfoceSettings;
			Project = InProject;
			State = InState;
			this.SynchronizationContext = SynchronizationContext.Current!;
			this.Logger = Logger;
			this.AsyncDisposer = ServiceProvider.GetRequiredService<IAsyncDisposer>();

			this.ProjectConfigFile = ProjectConfigFile;
			this.ProjectStreamFilter = ProjectStreamFilter;
		}

		public void Dispose()
		{
			CancelUpdate();
			if (PrevUpdateTask != null)
			{
				AsyncDisposer.Add(PrevUpdateTask);
			}
		}

		public ConfigFile ProjectConfigFile
		{
			get; private set;
		}

		public IReadOnlyList<string>? ProjectStreamFilter
		{
			get; private set;
		}

		CancellationTokenSource? PrevCancellationSource;
		Task PrevUpdateTask = Task.CompletedTask;

		public void Update(WorkspaceUpdateContext Context)
		{
			CancelUpdate();

			Task PrevUpdateTaskCopy = PrevUpdateTask;

			WorkspaceUpdate Update = new WorkspaceUpdate(Context);
			CurrentUpdate = Update;

			CancellationTokenSource CancellationSource = new CancellationTokenSource();
			PrevCancellationSource = CancellationSource;
			PrevUpdateTask = Task.Run(() => UpdateWorkspaceMini(Update, PrevUpdateTaskCopy, CancellationSource.Token));
		}

		public void CancelUpdate()
		{
			// Cancel the current task. We actually terminate the operation asynchronously, but we can signal the cancellation and 
			// send a cancelled event, then wait for the heavy lifting to finish in the new update task.
			if (PrevCancellationSource != null)
			{
				CancellationTokenSource PrevCancellationSourceCopy = PrevCancellationSource;
				PrevCancellationSourceCopy.Cancel();
				PrevUpdateTask = PrevUpdateTask.ContinueWith(Task => PrevCancellationSourceCopy.Dispose());
				PrevCancellationSource = null;
			}
			if(CurrentUpdate != null)
			{
				CompleteUpdate(CurrentUpdate, WorkspaceUpdateResult.Canceled, "Cancelled");
			}
		}

		async Task UpdateWorkspaceMini(WorkspaceUpdate Update, Task PrevUpdateTask, CancellationToken CancellationToken)
		{
			if (PrevUpdateTask != null)
			{
				await PrevUpdateTask;
			}

			WorkspaceUpdateContext Context = Update.Context;
			Context.ProjectConfigFile = ProjectConfigFile;
			Context.ProjectStreamFilter = ProjectStreamFilter;

			string StatusMessage;
			WorkspaceUpdateResult Result = WorkspaceUpdateResult.FailedToSync;

			try
			{
				(Result, StatusMessage) = await Update.ExecuteAsync(PerforceSettings, Project, State, Logger, CancellationToken);
				if (Result != WorkspaceUpdateResult.Success)
				{
					Logger.LogError("{Message}", StatusMessage);
				}
			}
			catch (OperationCanceledException)
			{
				StatusMessage = "Canceled.";
				Logger.LogError("Canceled.");
			}
			catch (Exception Ex)
			{
				StatusMessage = "Failed with exception - " + Ex.ToString();
				Logger.LogError(Ex, "Failed with exception");
			}

			ProjectConfigFile = Context.ProjectConfigFile;
			ProjectStreamFilter = Context.ProjectStreamFilter;

			SynchronizationContext.Post(x => CompleteUpdate(Update, Result, StatusMessage), null);
		}

		void CompleteUpdate(WorkspaceUpdate Update, WorkspaceUpdateResult Result, string StatusMessage)
		{
			if (CurrentUpdate == Update)
			{
				WorkspaceUpdateContext Context = Update.Context;

				State.SetLastSyncState(Result, Context, StatusMessage);
				State.Save();

				OnUpdateComplete?.Invoke(Context, Result, StatusMessage);
				CurrentUpdate = null;
			}
		}

		public Dictionary<string, string> GetVariables(BuildConfig EditorConfig, int? OverrideChange = null, int? OverrideCodeChange = null)
		{
			FileReference EditorReceiptFile = ConfigUtils.GetEditorReceiptFile(Project, ProjectConfigFile, EditorConfig);

			TargetReceipt? EditorReceipt;
			if (!ConfigUtils.TryReadEditorReceipt(Project, EditorReceiptFile, out EditorReceipt))
			{
				EditorReceipt = ConfigUtils.CreateDefaultEditorReceipt(Project, ProjectConfigFile, EditorConfig);
			}

			Dictionary<string, string> Variables = ConfigUtils.GetWorkspaceVariables(Project, OverrideChange ?? State.CurrentChangeNumber, OverrideCodeChange ?? State.CurrentCodeChangeNumber, EditorReceipt, ProjectConfigFile);
			return Variables;
		}

		public bool IsBusy()
		{
			return bSyncing;
		}

		public int CurrentChangeNumber => State.CurrentChangeNumber;

		public int PendingChangeNumber => CurrentUpdate?.Context?.ChangeNumber ?? CurrentChangeNumber;

		public string ClientName
		{
			get { return PerforceSettings.ClientName!; }
		}

		public Tuple<string, float> CurrentProgress => CurrentUpdate?.CurrentProgress ?? new Tuple<string, float>("", 0.0f);
	}
}
