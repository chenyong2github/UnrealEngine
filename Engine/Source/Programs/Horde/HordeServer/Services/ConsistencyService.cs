// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	/// <summary>
	/// Service which checks the database for consistency and fixes up any errors
	/// </summary>
	class ConsistencyService : ElectedBackgroundService
	{
		ISessionCollection SessionCollection;
		ILeaseCollection LeaseCollection;
		ILogger<ConsistencyService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">Database service</param>
		/// <param name="SessionCollection"></param>
		/// <param name="LeaseCollection"></param>
		/// <param name="Logger">Logging device</param>
		public ConsistencyService(DatabaseService DatabaseService, ISessionCollection SessionCollection, ILeaseCollection LeaseCollection, ILogger<ConsistencyService> Logger)
			: base(DatabaseService, new ObjectId("5fdb9d9f39b1291512821ef8"), Logger)
		{
			this.SessionCollection = SessionCollection;
			this.LeaseCollection = LeaseCollection;
			this.Logger = Logger;
		}

		/// <summary>
		/// Poll for inconsistencies in the database
		/// </summary>
		/// <param name="StoppingToken">Stopping token</param>
		/// <returns>Async task</returns>
		protected override async Task<DateTime> TickLeaderAsync(CancellationToken StoppingToken)
		{
			DateTime NextTickTime = DateTime.UtcNow + TimeSpan.FromMinutes(20.0);

			List<ISession> Sessions = await SessionCollection.FindActiveSessionsAsync();
			Dictionary<ObjectId, ISession> SessionIdToInstance = Sessions.ToDictionary(x => x.Id, x => x);

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

			return NextTickTime;
		}
	}
}
