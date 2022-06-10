// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	enum UpdateType
	{
		Background,
		UserInitiated,
	}

	class UpdateMonitor : IDisposable
	{
		Task? WorkerTask;
		CancellationTokenSource CancellationSource = new CancellationTokenSource();
		ILogger Logger;
		IAsyncDisposer AsyncDisposer;

		public Action<UpdateType>? OnUpdateAvailable;

		public bool? RelaunchUnstable
		{
			get;
			private set;
		}

		public UpdateMonitor(IPerforceSettings PerforceSettings, string? WatchPath, IServiceProvider ServiceProvider)
		{
			this.Logger = ServiceProvider.GetRequiredService<ILogger<UpdateMonitor>>();
			this.AsyncDisposer = ServiceProvider.GetRequiredService<IAsyncDisposer>();

			if(WatchPath != null)
			{
				Logger.LogInformation("Watching for updates on {WatchPath}", WatchPath);
				WorkerTask = Task.Run(() => PollForUpdatesAsync(PerforceSettings, WatchPath, CancellationSource.Token));
			}
		}

		public void Dispose()
		{
			OnUpdateAvailable = null;

			if (WorkerTask != null)
			{
				CancellationSource.Cancel();
				AsyncDisposer.Add(WorkerTask.ContinueWith(_ => CancellationSource.Dispose()));
				WorkerTask = null;
			}
		}

		public bool IsUpdateAvailable
		{
			get;
			private set;
		}

		async Task PollForUpdatesAsync(IPerforceSettings PerforceSettings, string WatchPath, CancellationToken CancellationToken)
		{
			for (; ; )
			{
				await Task.Delay(TimeSpan.FromMinutes(5.0), CancellationToken);

				IPerforceConnection? Perforce = null;
				try
				{
					Perforce = await PerforceConnection.CreateAsync(PerforceSettings, Logger);

					PerforceResponseList<ChangesRecord> Changes = await Perforce.TryGetChangesAsync(ChangesOptions.None, -1, ChangeStatus.Submitted, WatchPath, CancellationToken);
					if (Changes.Succeeded && Changes.Data.Count > 0)
					{
						TriggerUpdate(UpdateType.Background, null);
					}
				}
				catch (PerforceException ex)
				{
					Logger.LogInformation(ex, "Perforce exception while attempting to poll for updates.");
				}
				catch (Exception ex)
				{
					Logger.LogWarning(ex, "Exception while attempting to poll for updates.");
					Program.CaptureException(ex);
				}
				finally
				{
					Perforce?.Dispose();
				}
			}
		}

		public void TriggerUpdate(UpdateType UpdateType, bool? RelaunchUnstable)
		{
			this.RelaunchUnstable = RelaunchUnstable;
			IsUpdateAvailable = true;
			if(OnUpdateAvailable != null)
			{
				OnUpdateAvailable(UpdateType);
			}
		}
	}
}
