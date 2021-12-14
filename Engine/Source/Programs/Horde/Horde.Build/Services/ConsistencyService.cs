// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	using SessionId = ObjectId<ISession>;

	/// <summary>
	/// Service which checks the database for consistency and fixes up any errors
	/// </summary>
	class ConsistencyService : IHostedService, IDisposable
	{
		ISessionCollection SessionCollection;
		ILeaseCollection LeaseCollection;
		ITicker Ticker;
		ILogger<ConsistencyService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ConsistencyService(ISessionCollection SessionCollection, ILeaseCollection LeaseCollection, IClock Clock, ILogger<ConsistencyService> Logger)
		{
			this.SessionCollection = SessionCollection;
			this.LeaseCollection = LeaseCollection;
			this.Ticker = Clock.AddSharedTicker<ConsistencyService>(TimeSpan.FromMinutes(20.0), TickLeaderAsync, Logger);
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken cancellationToken) => Ticker.StartAsync();

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken cancellationToken) => Ticker.StopAsync();

		/// <inheritdoc/>
		public void Dispose() => Ticker.Dispose();

		/// <summary>
		/// Poll for inconsistencies in the database
		/// </summary>
		/// <param name="StoppingToken">Stopping token</param>
		/// <returns>Async task</returns>
		async ValueTask TickLeaderAsync(CancellationToken StoppingToken)
		{
			List<ISession> Sessions = await SessionCollection.FindActiveSessionsAsync();
			Dictionary<SessionId, ISession> SessionIdToInstance = Sessions.ToDictionary(x => x.Id, x => x);

			// Find any leases that are still running when their session has terminated
			List<ILease> Leases = await LeaseCollection.FindActiveLeasesAsync();
			foreach (ILease Lease in Leases)
			{
				if (!SessionIdToInstance.ContainsKey(Lease.SessionId))
				{
					ISession? Session = await SessionCollection.GetAsync(Lease.SessionId);
					DateTime FinishTime = Session?.FinishTime ?? DateTime.UtcNow;
					Logger.LogWarning("Setting finish time for lease {LeaseId} to {FinishTime}", Lease.Id, FinishTime);
					await LeaseCollection.TrySetOutcomeAsync(Lease.Id, FinishTime, LeaseOutcome.Cancelled, null);
				}
			}
		}
	}
}
