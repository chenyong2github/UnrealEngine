// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using HordeCommon.Rpc.Tasks;
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
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Service which updates telemetry periodically
	/// </summary>
	public class TelemetryService : ElectedBackgroundService
	{
		/// <summary>
		/// Collection of telemetry documents
		/// </summary>
		ITelemetryCollection TelemetryCollection;

		/// <summary>
		/// Collection of agent documents
		/// </summary>
		IAgentCollection AgentCollection;

		/// <summary>
		/// Collection of lease documents
		/// </summary>
		ILeaseCollection LeaseCollection;

		/// <summary>
		/// Collection of pool documents
		/// </summary>
		IPoolCollection PoolCollection;

		/// <summary>
		/// Logger for diagnostic messages
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		/// <param name="TelemetryCollection"></param>
		/// <param name="AgentCollection"></param>
		/// <param name="LeaseCollection"></param>
		/// <param name="PoolCollection"></param>
		/// <param name="Logger"></param>
		public TelemetryService(DatabaseService DatabaseService, ITelemetryCollection TelemetryCollection, IAgentCollection AgentCollection, ILeaseCollection LeaseCollection, IPoolCollection PoolCollection, ILogger<TelemetryService> Logger)
			: base(DatabaseService, new ObjectId("600082cb9b822d7afee6f0d1"), Logger)
		{
			this.TelemetryCollection = TelemetryCollection;
			this.AgentCollection = AgentCollection;
			this.LeaseCollection = LeaseCollection;
			this.PoolCollection = PoolCollection;
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		protected override async Task<DateTime> TickLeaderAsync(CancellationToken StoppingToken)
		{
			DateTime CurrentTime = DateTime.UtcNow;
			DateTime NextTickTime = CurrentTime + TimeSpan.FromMinutes(10.0);

			// Find the last time that we need telemetry for
			DateTime MaxTime = CurrentTime.Date + TimeSpan.FromHours(CurrentTime.Hour);

			// Get the latest telemetry data
			IUtilizationTelemetry? Latest = await TelemetryCollection.GetLatestUtilizationTelemetryAsync();
			TimeSpan Interval = TimeSpan.FromHours(1.0);
			int Count = (Latest == null) ? (7 * 24) : (int)Math.Round((MaxTime - Latest.FinishTime) / Interval);
			DateTime MinTime = MaxTime - Count * Interval;

			// Query all the current data
			List<IAgent> Agents = await AgentCollection.FindAsync();
			List<IPool> Pools = await PoolCollection.GetAsync();
			List<ILease> Leases = await LeaseCollection.FindLeasesAsync(MinTime: MinTime);

			// Find all the agents
			Dictionary<AgentId, List<PoolId>> AgentToPoolIds = Agents.ToDictionary(x => x.Id, x => x.GetPools().ToList());

			// Generate all the telemetry data
			DateTime BucketMinTime = MinTime;
			for (int Idx = 0; Idx < Count; Idx++)
			{
				DateTime BucketMaxTime = BucketMinTime + Interval;
				Logger.LogInformation("Creating telemetry for {MinTime} to {MaxTime}", BucketMinTime, BucketMaxTime);

				NewUtilizationTelemetry Telemetry = new NewUtilizationTelemetry(BucketMinTime, BucketMaxTime);
				Telemetry.NumAgents = Agents.Count;
				foreach (PoolId PoolId in AgentToPoolIds.Values.SelectMany(x => x))
				{
					Telemetry.FindOrAddPool(PoolId).NumAgents++;
				}
				foreach (ILease Lease in Leases)
				{
					if (Lease.StartTime < BucketMaxTime && Lease.FinishTime >= BucketMinTime)
					{
						List<PoolId>? LeasePools;
						if (AgentToPoolIds.TryGetValue(Lease.AgentId, out LeasePools))
						{
							DateTime FinishTime = Lease.FinishTime ?? BucketMaxTime;
							double Time = new TimeSpan(Math.Min(FinishTime.Ticks, BucketMaxTime.Ticks) - Math.Max(Lease.StartTime.Ticks, BucketMinTime.Ticks)).TotalHours;

							foreach (PoolId PoolId in LeasePools)
							{
								NewPoolUtilizationTelemetry PoolTelemetry = Telemetry.FindOrAddPool(PoolId);
								if (Lease.PoolId == null || Lease.StreamId == null)
								{
									PoolTelemetry.AdminTime += Time;
								}
								else if (PoolId == Lease.PoolId)
								{
									PoolTelemetry.FindOrAddStream(Lease.StreamId.Value).Time += Time;
								}
								else
								{
									PoolTelemetry.OtherTime += Time;
								}
							}
						}
					}
				}
				await TelemetryCollection.AddUtilizationTelemetryAsync(Telemetry);

				BucketMinTime = BucketMaxTime;
			}

			return NextTickTime;
		}
	}
}
