// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Services;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Service which is ticked on one machine in the cluster at a specified interval
	/// </summary>
	public sealed class ElectedTick : IAsyncDisposable, IDisposable
	{
		class State : SingletonBase
		{
			public string? Host { get; set; }
			public DateTime NextUpdateTime { get; set; }
		}

		SingletonDocument<State> StateAccessor;
		TimeSpan PollInterval;
		TimeSpan ElectionTimeout;
		Task? BackgroundTask;
		CancellationTokenSource CancellationTokenSource;
		Func<CancellationToken, Task<DateTime>> TickAsync;
		bool ReadOnlyMode;
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="ServiceId">Object id for the singleton tracking the update state</param>
		/// <param name="TickAsync">The tick function to be called</param>
		/// <param name="Interval">Interval for calling the tick function</param>
		/// <param name="Logger">The log service</param>
		public ElectedTick(DatabaseService DatabaseService, ObjectId ServiceId, Func<CancellationToken, Task> TickAsync, TimeSpan Interval, ILogger Logger)
			: this(DatabaseService, ServiceId, Interval, Interval, async (Token) => { await TickAsync(Token); return DateTime.UtcNow + Interval; }, Logger)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="ServiceId">Object id for the singleton tracking the update state</param>
		/// <param name="TickAsync">The tick function to be called</param>
		/// <param name="Logger">The log service</param>
		public ElectedTick(DatabaseService DatabaseService, ObjectId ServiceId, Func<CancellationToken, Task<DateTime>> TickAsync, ILogger Logger)
			: this(DatabaseService, ServiceId, TimeSpan.FromMinutes(1.0), TimeSpan.FromMinutes(5.0), TickAsync, Logger)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="ServiceId">Object id for the singleton tracking the update state</param>
		/// <param name="PollInterval">Frequency to poll state</param>
		/// <param name="ElectionTimeout"></param>
		/// <param name="TickAsync">The tick function to be called</param>
		/// <param name="Logger">The log service</param>
		public ElectedTick(DatabaseService DatabaseService, ObjectId ServiceId, TimeSpan PollInterval, TimeSpan ElectionTimeout, Func<CancellationToken, Task<DateTime>> TickAsync, ILogger Logger)
		{
			this.StateAccessor = new SingletonDocument<State>(DatabaseService, ServiceId);
			this.PollInterval = PollInterval;
			this.ElectionTimeout = ElectionTimeout;
			this.TickAsync = TickAsync;
			this.ReadOnlyMode = DatabaseService.ReadOnlyMode;
			this.Logger = Logger;

			CancellationTokenSource = new CancellationTokenSource();
		}

		/// <summary>
		/// Start ticking the callback
		/// </summary>
		/// <returns></returns>
		public void Start()
		{
			if (BackgroundTask == null)
			{
				BackgroundTask = Task.Run(() => RunAsync(CancellationTokenSource.Token));
			}
		}

		/// <summary>
		/// Stop ticking the callback
		/// </summary>
		/// <returns></returns>
		public async Task StopAsync()
		{
			if (BackgroundTask != null)
			{
				CancellationTokenSource.Cancel();
				try
				{
					await BackgroundTask;
					BackgroundTask = null!;
				}
				catch (OperationCanceledException)
				{
				}
			}
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			DisposeAsync().AsTask().Wait();
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			await StopAsync();
			CancellationTokenSource.Dispose();
		}

		/// <summary>
		/// Tick the local service, and poll for whether to tick the shared service.
		/// </summary>
		/// <param name="StoppingToken">Stopping token</param>
		/// <returns>Async task</returns>
		async Task RunAsync(CancellationToken StoppingToken)
		{
			for (; ; )
			{
				DateTime UtcNow = DateTime.UtcNow;
				State State = await StateAccessor.GetAsync();

				// Check if it's time to update yet
				TimeSpan Delay = State.NextUpdateTime - UtcNow;
				if (Delay > TimeSpan.Zero)
				{
					TimeSpan RetryDelay = Delay + TimeSpan.FromSeconds(0.5);
					if (PollInterval < RetryDelay)
					{
						RetryDelay = PollInterval;
					}
					await Task.Delay(RetryDelay);
					continue;
				}

				// Don't claim the election unless we're in writeable mode
				if (ReadOnlyMode)
				{
					continue;
				}

				// Try to claim the election
				State.Host = System.Net.Dns.GetHostName();
				State.NextUpdateTime = UtcNow + ElectionTimeout;
				if (!await StateAccessor.TryUpdateAsync(State))
				{
					continue;
				}

				// Start the tick function, and keep prolonging the next update time until it finishes
				DateTime NextUpdateTime;
				try
				{
					Task<DateTime> TickTask = Task.Run(() => TickAsync(StoppingToken));
					while (await Task.WhenAny(TickTask, Task.Delay(ElectionTimeout / 2)) != TickTask)
					{
						State.NextUpdateTime = DateTime.UtcNow + ElectionTimeout;
						await StateAccessor.TryUpdateAsync(State);
					}
					NextUpdateTime = await TickTask;
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Exception while executing tick function");
					continue;
				}

				// Update the next update time
				State.NextUpdateTime = NextUpdateTime;
				await StateAccessor.TryUpdateAsync(State);
			}
		}
	}
}
