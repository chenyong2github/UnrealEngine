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
	public abstract class ElectedBackgroundService : TickedBackgroundService
	{
		class State : SingletonBase
		{
			public string? Host { get; set; }
			public DateTime NextUpdateTime { get; set; }
		}

		SingletonDocument<State> StateAccessor;
		TimeSpan PollInterval;
		TimeSpan ElectionTimeout;
		bool ReadOnlyMode;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="ServiceId">Object id for the singleton tracking the update state</param>
		/// <param name="Logger">Logging device</param>
		public ElectedBackgroundService(DatabaseService DatabaseService, ObjectId ServiceId, ILogger Logger)
			: this(DatabaseService, ServiceId, TimeSpan.FromMinutes(1.0), TimeSpan.FromMinutes(5.0), Logger)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="ServiceId">Object id for the singleton tracking the update state</param>
		/// <param name="PollInterval">Frequency to poll state</param>
		/// <param name="ElectionTimeout"></param>
		/// <param name="Logger">Logging device</param>
		public ElectedBackgroundService(DatabaseService DatabaseService, ObjectId ServiceId, TimeSpan PollInterval, TimeSpan ElectionTimeout, ILogger Logger)
			: base(TimeSpan.FromSeconds(10.0), Logger)
		{
			this.StateAccessor = new SingletonDocument<State>(DatabaseService, ServiceId);
			this.PollInterval = PollInterval;
			this.ElectionTimeout = ElectionTimeout;
			this.ReadOnlyMode = DatabaseService.ReadOnlyMode;
		}

		/// <summary>
		/// Tick the local service, and poll for whether to tick the shared service.
		/// </summary>
		/// <param name="StoppingToken">Stopping token</param>
		/// <returns>Async task</returns>
		protected override sealed async Task TickAsync(CancellationToken StoppingToken)
		{
			if (ReadOnlyMode)
			{
				return;
			}

			DateTime UtcNow = DateTime.UtcNow;
			for (; ; )
			{
				State State = await StateAccessor.GetAsync();

				// Check if it's time to update yet
				TimeSpan Delay = State.NextUpdateTime - UtcNow;
				if(Delay > TimeSpan.Zero)
				{
					TimeSpan RetryDelay = Delay + TimeSpan.FromSeconds(0.5);
					if (PollInterval < RetryDelay)
					{
						RetryDelay = PollInterval;
					}

					Interval = RetryDelay;
					break;
				}

				// Try to become the leader and execute the update
				State.Host = System.Net.Dns.GetHostName();
				State.NextUpdateTime = UtcNow + ElectionTimeout;

				if (await StateAccessor.TryUpdateAsync(State))
				{
					// Update the next update time while we're still running the tick
					Task<DateTime> TickTask = Task.Run(() => TickLeaderAsync(StoppingToken));
					while (await Task.WhenAny(TickTask, Task.Delay(ElectionTimeout / 2)) != TickTask)
					{
						State.NextUpdateTime = DateTime.UtcNow + ElectionTimeout;
						await StateAccessor.TryUpdateAsync(State);
					}
					State.NextUpdateTime = await TickTask;
					await StateAccessor.TryUpdateAsync(State);
				}
			}
		}

		/// <summary>
		/// Tick the shared service
		/// </summary>
		/// <param name="StoppingToken">Stopping token</param>
		/// <returns>Async task</returns>
		protected abstract Task<DateTime> TickLeaderAsync(CancellationToken StoppingToken);
		
		/// <summary>
		/// Tick the implemented method from above
		/// **Only for use in testing!**
		/// </summary>
		/// <param name="StoppingToken">Optional token signalling that the task should terminate</param>
		/// <returns>Async task</returns>
		public Task TickSharedOnlyForTestingAsync(CancellationToken? StoppingToken = null)
		{
			return TickLeaderAsync(StoppingToken ?? new CancellationToken());
		}
	}
}
