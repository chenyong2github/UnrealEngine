// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using OpenTracing;
using OpenTracing.Util;

namespace Horde.Build.Fleet.Autoscale
{
	using PoolId = StringId<IPool>;
	
	/// <summary>
	/// Calculate pool size by looking at previously finished leases
	/// </summary>
	public class LeaseUtilizationStrategy : IPoolSizeStrategy
	{
		struct UtilizationSample
		{
			public double JobWork;
			public double OtherWork;
		}

		class AgentData
		{
			public IAgent Agent { get; }
			public UtilizationSample[] Samples { get; }
			public List<ILease> Leases { get; } = new ();
			private readonly int NumSamples;

			public AgentData(IAgent Agent, int NumSamples)
			{
				this.Agent = Agent;
				this.NumSamples = NumSamples;
				Samples = new UtilizationSample[NumSamples];
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
			public List<IAgent> Agents { get; } = new ();
			public UtilizationSample[] Samples { get; }

			public PoolData(IPool Pool, int NumSamples)
			{
				this.Pool = Pool;
				Samples = new UtilizationSample[NumSamples];
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
		
		private readonly IAgentCollection AgentCollection;
		private readonly IPoolCollection PoolCollection;
		private readonly ILeaseCollection LeaseCollection;
		private readonly IClock Clock;
		private readonly TimeSpan SampleTime = TimeSpan.FromMinutes(6.0);
		private readonly int NumSamples = 10;
		private readonly int NumSamplesForResult = 9;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AgentCollection"></param>
		/// <param name="PoolCollection"></param>
		/// <param name="LeaseCollection"></param>
		/// <param name="Clock"></param>
		/// <param name="NumSamples">Number of samples to collect for calculating lease utilization</param>
		/// <param name="NumSamplesForResult">Min number of samples for a valid result</param>
		/// <param name="SampleTime">Time period for each sample</param>
		public LeaseUtilizationStrategy(IAgentCollection AgentCollection, IPoolCollection PoolCollection, ILeaseCollection LeaseCollection, IClock Clock, int NumSamples = 10, int NumSamplesForResult = 9, TimeSpan? SampleTime = null)
		{
			this.AgentCollection = AgentCollection;
			this.PoolCollection = PoolCollection;
			this.LeaseCollection = LeaseCollection;
			this.Clock = Clock;
			this.NumSamples = NumSamples;
			this.NumSamplesForResult = NumSamplesForResult;
			this.SampleTime = SampleTime ?? this.SampleTime;
		}

		private async Task<Dictionary<AgentId, AgentData>> GetAgentDataAsync()
		{
			using IScope _ = GlobalTracer.Instance.BuildSpan("GetAgentDataAsync").StartActive();
			
			// Find all the current agents
			List<IAgent> Agents = await AgentCollection.FindAsync(Status: AgentStatus.Ok);

			// Query leases in last interval
			DateTime MaxTime = Clock.UtcNow;
			DateTime MinTime = MaxTime - (SampleTime * NumSamples);
			List<ILease> Leases = await LeaseCollection.FindLeasesAsync(MinTime, MaxTime);

			// Add all the leases to a data object for each agent
			Dictionary<AgentId, AgentData> AgentIdToData = Agents.ToDictionary(x => x.Id, x => new AgentData(x, NumSamples));
			foreach (ILease Lease in Leases)
			{
				AgentData? AgentData;
				if (AgentIdToData.TryGetValue(Lease.AgentId, out AgentData) && AgentData.Agent.SessionId == Lease.SessionId)
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

			return AgentIdToData;
		}
		
		private async Task<Dictionary<PoolId, PoolData>> GetPoolDataAsync()
		{
			using IScope _ = GlobalTracer.Instance.BuildSpan("GetPoolDataAsync").StartActive();

			Dictionary<AgentId, AgentData> AgentIdToData = await GetAgentDataAsync();
			
			// Get all the pools
			List<IPool> Pools = await PoolCollection.GetAsync();
			Dictionary<PoolId, PoolData> PoolToData = Pools.ToDictionary(x => x.Id, x => new PoolData(x, NumSamples));

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

			return PoolToData;
		}

		/// <inheritdoc/>
		public string Name { get; } = "LeaseUtilization";

		/// <inheritdoc/>
		public async Task<List<PoolSizeData>> CalcDesiredPoolSizesAsync(List<PoolSizeData> Pools)
		{
			Dictionary<PoolId, PoolData> PoolToData = await GetPoolDataAsync();
			List<PoolSizeData> Result = new();

			foreach (PoolData PoolData in PoolToData.Values.OrderByDescending(x => x.Agents.Count))
			{
				IPool Pool = PoolData.Pool;

				PoolSizeData? PoolSize = Pools.Find(x => x.Pool.Id == Pool.Id);
				if (PoolSize != null)
				{
					int MinAgents = Pool.MinAgents ?? 1;
					int NumReserveAgents = Pool.NumReserveAgents ?? 5;
					double Utilization = PoolData.Samples.Select(x => x.JobWork).OrderByDescending(x => x).Skip(NumSamples - NumSamplesForResult).First();
				
					// Number of agents in use over the sampling period. Can never be greater than number of agents available in pool.
					int NumAgentsUtilized = (int)Utilization;
					
					// Include reserve agent count to ensure pool always can grow
					int DesiredAgentCount = Math.Max(NumAgentsUtilized + NumReserveAgents, MinAgents);;
					
					StringBuilder Sb = new();
					Sb.AppendFormat("Jobs=[{0}] ", GetDensityMap(PoolData.Samples.Select(x => x.JobWork / Math.Max(1, PoolData.Agents.Count))));
					Sb.AppendFormat("Total=[{0}] ", GetDensityMap(PoolData.Samples.Select(x => x.OtherWork / Math.Max(1, PoolData.Agents.Count))));
					Sb.AppendFormat("Min=[{0,5:0.0}] ", PoolData.Samples.Min(x => x.JobWork));
					Sb.AppendFormat("Max=[{0,5:0.0}] ", PoolData.Samples.Max(x => x.JobWork));
					Sb.AppendFormat("Avg=[{0,5:0.0}] ", Utilization);
					Sb.AppendFormat("Pct=[{0,5:0.0}] ", PoolData.Samples.Sum(x => x.JobWork) / NumSamples);

					Result.Add(new(Pool, PoolSize.Agents, DesiredAgentCount, Sb.ToString()));
				}
			}

			return Result;
		}
		
		/// <summary>
		/// Creates a string of characters indicating a sequence of 0-1 values over time
		/// </summary>
		/// <param name="Values">Sequence of values</param>
		/// <returns>Density map</returns>
		private static string GetDensityMap(IEnumerable<double> Values)
		{
			const string Greyscale = " 123456789";
			return new string(Values.Select(x => Greyscale[Math.Clamp((int)(x * Greyscale.Length), 0, Greyscale.Length - 1)]).ToArray());
		}
	}
}
