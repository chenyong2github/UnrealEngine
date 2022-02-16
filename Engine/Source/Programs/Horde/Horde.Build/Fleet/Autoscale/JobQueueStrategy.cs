// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using HordeCommon;
using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;

namespace Horde.Build.Fleet.Autoscale
{
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;
	
	/// <summary>
	/// Calculate pool size by observing the number of jobs in waiting state
	///
	/// Allows for more proactive scaling compared to LeaseUtilizationStrategy.
	/// <see cref="Horde.Build.Fleet.Autoscale.LeaseUtilizationStrategy"/> 
	/// </summary>
	public class JobQueueStrategy : IPoolSizeStrategy
	{
		
		private readonly IJobCollection Jobs;
		private readonly IGraphCollection Graphs;
		private readonly StreamService StreamService;
		private readonly IClock Clock;
			
		/// <summary>
		/// How far back in time to look for job batches (that potentially are in the queue)
		/// </summary>
		private readonly TimeSpan SamplePeriod = TimeSpan.FromHours(2.0);
		
		/// <summary>
		/// Time spent in ready state before considered truly waiting for an agent
		///
		/// A job batch can be in ready state before getting picked up and executed.
		/// This threshold will help ensure only batches that have been waiting longer than this value will be considered.
		/// </summary>
		internal readonly TimeSpan ReadyTimeThreshold = TimeSpan.FromSeconds(45.0);
		
		/// <summary>
		/// Minimum number of jobs in queue for scale-out logic to activate
		///
		/// Useful to avoid a very small queue size triggering any scaling. 
		/// </summary>
		private readonly int MinQueueSizeForScaleOut = 3;

		/// <summary>
		/// Factor translating queue size to additional agents to grow the pool with
		///
		/// Example: if there are 20 jobs in queue, a factor 0.25 will result in 5 new agents being added (20 * 0.25)
		/// </summary>
		private readonly double ScaleOutFactor = 0.25;
		
		/// <summary>
		/// Factor by which to shrink the pool size with when queue is empty
		///
		/// Example: when the queue size is zero, a default value of 0.9 will shrink the pool by 10% (current agent count * 0.9)
		/// </summary>
		private readonly double ScaleInFactor = 0.9;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Jobs"></param>
		/// <param name="Graphs"></param>
		/// <param name="StreamService"></param>
		/// <param name="Clock"></param>
		/// <param name="SamplePeriod">Time period for each sample</param>
		public JobQueueStrategy(IJobCollection Jobs, IGraphCollection Graphs, StreamService StreamService, IClock Clock, TimeSpan? SamplePeriod = null)
		{
			this.Jobs = Jobs;
			this.Graphs = Graphs;
			this.StreamService = StreamService;
			this.Clock = Clock;
			this.SamplePeriod = SamplePeriod ?? this.SamplePeriod;
		}

		/// <inheritdoc/>
		public string Name { get; } = "JobQueue";

		/// <summary>
		/// Extract all job step batches from a job, with their associated pool 
		/// </summary>
		/// <param name="Job">Job to extract from</param>
		/// <param name="Streams">Cached lookup table of streams</param>
		/// <returns></returns>
		private async Task<List<(IJob Job, IJobStepBatch Batch, PoolId PoolId)>> GetJobBatchesWithPools(IJob Job, Dictionary<StreamId, IStream> Streams)
		{
			IGraph Graph = await Graphs.GetAsync(Job.GraphHash);

			List<(IJob Job, IJobStepBatch Batch, PoolId PoolId)> JobBatches = new();
			foreach (IJobStepBatch Batch in Job.Batches)
			{
				if (Batch.State != JobStepBatchState.Ready) continue;
				
				TimeSpan? WaitTime = Clock.UtcNow - Batch.ReadyTimeUtc;
				if (WaitTime == null) continue;
				if (WaitTime.Value < ReadyTimeThreshold) continue;

				if (!Streams.TryGetValue(Job.StreamId, out IStream? Stream))
				{
					continue;
				}

				string BatchAgentType = Graph.Groups[Batch.GroupIdx].AgentType;
				if (!Stream.AgentTypes.TryGetValue(BatchAgentType, out AgentType? AgentType))
				{
					continue;
				}
				
				JobBatches.Add((Job, Batch, AgentType.Pool));
			}

			return JobBatches;
		}

		internal async Task<Dictionary<PoolId, int>> GetPoolQueueSizesAsync(DateTimeOffset JobsCreatedAfter)
		{
			List<IStream> StreamsList = await StreamService.GetStreamsAsync();
			Dictionary<StreamId, IStream> Streams = StreamsList.ToDictionary(x => x.Id, x => x);
			List<IJob> RecentJobs = await Jobs.FindAsync(MinCreateTime: JobsCreatedAfter);

			List<(IJob Job, IJobStepBatch Batch, PoolId PoolId)> JobBatches = new();
			foreach (IJob Job in RecentJobs)
			{
				JobBatches.AddRange(await GetJobBatchesWithPools(Job, Streams));
			}

			List<(PoolId PoolId, int QueueSize)> PoolsWithQueueSize = JobBatches.GroupBy(t => t.PoolId).Select(t => (t.Key, t.Count())).ToList();
			return PoolsWithQueueSize.ToDictionary(x => x.PoolId, x => x.QueueSize);
		}

		/// <inheritdoc/>
		public async Task<List<PoolSizeData>> CalcDesiredPoolSizesAsync(List<PoolSizeData> Pools)
		{
			DateTimeOffset MinCreateTime = Clock.UtcNow - SamplePeriod;
			Dictionary<PoolId, int> PoolQueueSizes = await GetPoolQueueSizesAsync(MinCreateTime);

			return Pools.Select(Current =>
			{
				PoolQueueSizes.TryGetValue(Current.Pool.Id, out var QueueSize);
				if (QueueSize > 0)
				{
					int AdditionalAgentCount = (int)Math.Round(QueueSize * ScaleOutFactor);
					if (QueueSize < MinQueueSizeForScaleOut)
						AdditionalAgentCount = 0;

					int DesiredAgentCount = Current.Agents.Count + AdditionalAgentCount;
					return new PoolSizeData(Current.Pool, Current.Agents, DesiredAgentCount, $"QueueSize={QueueSize}");
				}
				else
				{
					int DesiredAgentCount = (int)(Current.Agents.Count * ScaleInFactor);
					return new PoolSizeData(Current.Pool, Current.Agents, DesiredAgentCount, "Empty job queue");
				}
			}).ToList();
		}
	}
}
