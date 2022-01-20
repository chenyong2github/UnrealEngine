// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
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
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Service which updates telemetry periodically
	/// </summary>
	public sealed class TelemetryService : IHostedService, IDisposable
	{
		ITelemetryCollection TelemetryCollection;
		IAgentCollection AgentCollection;
		ILeaseCollection LeaseCollection;
		IPoolCollection PoolCollection;
		IFleetManager FleetManager;
		IClock Clock;
		ITicker Tick;
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public TelemetryService(ITelemetryCollection TelemetryCollection, IAgentCollection AgentCollection, ILeaseCollection LeaseCollection, IPoolCollection PoolCollection, IFleetManager FleetManager, IClock Clock, ILogger<TelemetryService> Logger)
		{
			this.TelemetryCollection = TelemetryCollection;
			this.AgentCollection = AgentCollection;
			this.LeaseCollection = LeaseCollection;
			this.PoolCollection = PoolCollection;
			this.FleetManager = FleetManager;
			this.Clock = Clock;
			this.Tick = Clock.AddSharedTicker<TelemetryService>(TimeSpan.FromMinutes(10.0), TickLeaderAsync, Logger);
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken CancellationToken) => Tick.StartAsync();

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken CancellationToken) => Tick.StopAsync();

		/// <inheritdoc/>
		public void Dispose() => Tick.Dispose();

		/// <inheritdoc/>
		async ValueTask TickLeaderAsync(CancellationToken StoppingToken)
		{
			DateTime CurrentTime = Clock.UtcNow;

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

			// Remove any agents which are offline
			Agents.RemoveAll(x => !x.Enabled || !x.IsSessionValid(CurrentTime));

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
				foreach (IPool Pool in Pools)
				{
					if (Pool.EnableAutoscaling)
					{
						int NumStoppedInstances = await FleetManager.GetNumStoppedInstancesAsync(Pool);
						Telemetry.NumAgents += NumStoppedInstances;

						NewPoolUtilizationTelemetry PoolTelemetry = Telemetry.FindOrAddPool(Pool.Id);
						PoolTelemetry.NumAgents += NumStoppedInstances;
						PoolTelemetry.HibernatingTime += Interval.TotalHours * NumStoppedInstances;
					}
				}
				foreach (PoolId PoolId in AgentToPoolIds.Values.SelectMany(x => x))
				{
					Telemetry.FindOrAddPool(PoolId).NumAgents++;
				}
				foreach (ILease Lease in Leases)
				{
					if (Lease.StartTime < BucketMaxTime && (!Lease.FinishTime.HasValue || Lease.FinishTime >= BucketMinTime))
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
		}
	}
}
