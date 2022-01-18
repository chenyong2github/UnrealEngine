// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon;
using Amazon.EC2;
using Amazon.EC2.Model;
using Amazon.Runtime;
using Amazon.Runtime.CredentialManagement;
using Google.Protobuf.WellKnownTypes;
using HordeCommon.Rpc.Tasks;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using HordeCommon;
using Microsoft.Extensions.Options;
using OpenTracing;
using OpenTracing.Util;
using StatsdClient;
using Microsoft.Extensions.Hosting;

namespace HordeServer.Services
{
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Service for managing the autoscaling of agent pools
	/// </summary>
	public sealed class AutoscaleService : IHostedService, IDisposable
	{
		const int NumSamples = 10;
		const int NumSamplesForResult = 9;
		static readonly TimeSpan SampleTime = TimeSpan.FromMinutes(6.0);
		static readonly TimeSpan ShrinkPoolCoolDown = TimeSpan.FromMinutes(20.0);

		struct UtilizationSample
		{
			public double JobWork;
			public double OtherWork;
		}

		class AgentData
		{
			public IAgent Agent { get; }
			public UtilizationSample[] Samples { get; } = new UtilizationSample[NumSamples];
			public List<ILease> Leases { get; } = new List<ILease>();

			public AgentData(IAgent Agent)
			{
				this.Agent = Agent;
			}

			public void Add(double MinT, double MaxT, double JobWork, double OtherWork)
			{
				int MinIdx = Math.Clamp((int)MinT, 0, NumSamples - 1);
				int MaxIdx = Math.Clamp((int)MaxT, MinIdx, NumSamples - 1);
				for (int Idx = MinIdx; Idx <= MaxIdx; Idx++)
				{
					double Fraction = Math.Clamp(MaxT - Idx, 0.0, 1.0) - Math.Clamp(MinT - Idx, 0.0, 1.0);
					Samples[Idx].JobWork += JobWork * Fraction;
					Samples[Idx].OtherWork += OtherWork * Fraction;
				}
			}
		}

		class PoolData
		{
			public IPool Pool { get; }
			public List<IAgent> Agents { get; } = new List<IAgent>();
			public UtilizationSample[] Samples { get; } = new UtilizationSample[NumSamples];

			public PoolData(IPool Pool)
			{
				this.Pool = Pool;
			}

			public void Add(AgentData AgentData)
			{
				Agents.Add(AgentData.Agent);
				for (int Idx = 0; Idx < Samples.Length; Idx++)
				{
					Samples[Idx].JobWork += AgentData.Samples[Idx].JobWork;
					Samples[Idx].OtherWork += AgentData.Samples[Idx].OtherWork;
				}
			}
		}

		IAgentCollection AgentCollection;
		IPoolCollection PoolCollection;
		ILeaseCollection LeaseCollection;
		IFleetManager FleetManager;
		IDogStatsd DogStatsd;
		IClock Clock;
		ITicker Ticker;
		ILogger<AutoscaleService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public AutoscaleService(IAgentCollection AgentCollection, IPoolCollection PoolCollection, ILeaseCollection LeaseCollection, IFleetManager FleetManager, IDogStatsd DogStatsd, DatabaseService DatabaseService, IClock Clock, ILogger<AutoscaleService> Logger)
		{
			this.AgentCollection = AgentCollection;
			this.PoolCollection = PoolCollection;
			this.LeaseCollection = LeaseCollection;
			this.FleetManager = FleetManager;
			this.DogStatsd = DogStatsd;
			this.Clock = Clock;
			if (DatabaseService.ReadOnlyMode)
			{
				this.Ticker = new NullTicker();
			}
			else
			{
				this.Ticker = Clock.AddSharedTicker<AutoscaleService>(TimeSpan.FromMinutes(5.0), TickLeaderAsync, Logger);
			}
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken CancellationToken) => Ticker.StartAsync();

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken CancellationToken) => Ticker.StopAsync();

		/// <inheritdoc/>
		public void Dispose() => Ticker.Dispose();

		/// <inheritdoc/>
		async ValueTask TickLeaderAsync(CancellationToken StoppingToken)
		{
			DateTime UtcNow = Clock.UtcNow;

			Logger.LogInformation("Autoscaling pools...");
			Stopwatch Stopwatch = Stopwatch.StartNew();

			Dictionary<PoolId, PoolData> PoolToData;
			using (IScope Scope = GlobalTracer.Instance.BuildSpan("Compute utilization").StartActive())
			{
				// Find all the current agents
				List<IAgent> Agents = await AgentCollection.FindAsync(Status: AgentStatus.Ok);

				// Query leases in last interval
				DateTime MaxTime = Clock.UtcNow;
				DateTime MinTime = MaxTime - (SampleTime * NumSamples);
				List<ILease> Leases = await LeaseCollection.FindLeasesAsync(MinTime, MaxTime);

				// Add all the leases to a data object for each agent
				Dictionary<AgentId, AgentData> AgentIdToData = Agents.ToDictionary(x => x.Id, x => new AgentData(x));
				foreach (ILease Lease in Leases)
				{
					AgentData? AgentData;
					if (AgentIdToData.TryGetValue(Lease.AgentId, out AgentData) &&
					    AgentData.Agent.SessionId == Lease.SessionId)
					{
						AgentData.Leases.Add(Lease);
					}
				}

				// Compute utilization for each agent
				foreach (AgentData AgentData in AgentIdToData.Values)
				{
					foreach (ILease Lease in AgentData.Leases.OrderBy(x => x.StartTime))
					{
						double MinT = (Lease.StartTime - MinTime).TotalSeconds / SampleTime.TotalSeconds;
						double MaxT = (Lease.FinishTime == null)
							? NumSamples
							: ((Lease.FinishTime.Value - MinTime).TotalSeconds / SampleTime.TotalSeconds);

						Any Payload = Any.Parser.ParseFrom(Lease.Payload.ToArray());
						if (Payload.Is(ExecuteJobTask.Descriptor))
						{
							AgentData.Add(MinT, MaxT, 1.0, 0.0);
						}
						else
						{
							AgentData.Add(MinT, MaxT, 0.0, 1.0);
						}
					}
				}

				// Get all the pools
				List<IPool> Pools = await PoolCollection.GetAsync();
				PoolToData = Pools.ToDictionary(x => x.Id, x => new PoolData(x));

				// Find pool utilization over the query period
				foreach (AgentData AgentData in AgentIdToData.Values)
				{
					foreach (PoolId PoolId in AgentData.Agent.GetPools())
					{
						PoolData? PoolData;
						if (PoolToData.TryGetValue(PoolId, out PoolData))
						{
							PoolData.Add(AgentData);
						}
					}
				}
			}

			// Output all the final stats
			foreach (PoolData PoolData in PoolToData.Values.OrderByDescending(x => x.Agents.Count))
			{
				IPool Pool = PoolData.Pool;

				int MinAgents = Pool.MinAgents ?? 1;
				int NumReserveAgents = Pool.NumReserveAgents ?? 5;

				double Utilization = PoolData.Samples.Select(x => x.JobWork).OrderByDescending(x => x).Skip(NumSamples - NumSamplesForResult).First();

				int TargetAgents = Math.Max((int)Utilization + NumReserveAgents, MinAgents);
				int Delta = TargetAgents - PoolData.Agents.Count;

				Logger.LogInformation("{PoolName,-48} Jobs=[{JobUtilization}] Total=[{OtherUtilization}] Min={Min,5:0.0} Max={Max,5:0.0} Avg={Avg,5:0.0} Pct={Pct,5:0.0} Current={Current,4} Target={Target,4} Delta={Delta}",
					Pool.Name,
					GetDensityMap(PoolData.Samples.Select(x => x.JobWork / Math.Max(1, PoolData.Agents.Count))),
					GetDensityMap(PoolData.Samples.Select(x => x.OtherWork / Math.Max(1, PoolData.Agents.Count))),
					PoolData.Samples.Min(x => x.JobWork),
					PoolData.Samples.Max(x => x.JobWork),
					Utilization,
					PoolData.Samples.Sum(x => x.JobWork) / NumSamples,
					PoolData.Agents.Count,
					TargetAgents,
					Delta);

				try
				{
					if (Pool.EnableAutoscaling)
					{
						using (IScope Scope = GlobalTracer.Instance.BuildSpan("Scaling pool").StartActive())
						{
							Scope.Span.SetTag("poolName", Pool.Name);
							Scope.Span.SetTag("delta", Delta);

							if (Delta > 0)
							{
								await FleetManager.ExpandPoolAsync(Pool, PoolData.Agents, Delta);
								await PoolCollection.TryUpdateAsync(Pool, LastScaleUpTime: DateTime.UtcNow);
							}

							if (Delta < 0)
							{
								bool bShrinkIsOnCoolDown = Pool.LastScaleDownTime != null && Pool.LastScaleDownTime + ShrinkPoolCoolDown > DateTime.UtcNow;
								if (!bShrinkIsOnCoolDown)
								{
									await FleetManager.ShrinkPoolAsync(Pool, PoolData.Agents, -Delta);
									await PoolCollection.TryUpdateAsync(Pool, LastScaleDownTime: DateTime.UtcNow);
								}
								else
								{
									Logger.LogDebug("Cannot shrink {PoolName} right now, it's on cool-down until {CoolDownTimeEnds}", Pool.Name, Pool.LastScaleDownTime + ShrinkPoolCoolDown);
								}
							}
						}
					}
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Failed to scale {PoolName}:\n{Exception}", Pool.Name, Ex);
					continue;
				}

				DogStatsd.Gauge("agentpools.autoscale.target", TargetAgents, tags: new []{"pool:" + Pool.Name});
				DogStatsd.Gauge("agentpools.autoscale.current", PoolData.Agents.Count, tags: new []{"pool:" + Pool.Name});
			}
			
			Stopwatch.Stop();
			Logger.LogInformation("Autoscaling pools took {ElapsedTime} ms", Stopwatch.ElapsedMilliseconds);
		}

		/// <summary>
		/// Creates a string of characters indicating a sequence of 0-1 values over time
		/// </summary>
		/// <param name="Values">Sequence of values</param>
		/// <returns>Density map</returns>
		static string GetDensityMap(IEnumerable<double> Values)
		{
			const string Greyscale = " 123456789";
			return new string(Values.Select(x => Greyscale[Math.Clamp((int)(x * Greyscale.Length), 0, Greyscale.Length - 1)]).ToArray());
		}
	}
}
