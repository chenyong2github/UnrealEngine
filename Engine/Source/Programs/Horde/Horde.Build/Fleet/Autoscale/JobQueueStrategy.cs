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
		private readonly TimeSpan LookbackPeriod = TimeSpan.FromHours(2.0);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Jobs"></param>
		/// <param name="Graphs"></param>
		/// <param name="StreamService"></param>
		/// <param name="Clock"></param>
		/// <param name="LookbackPeriod">Time period for each sample</param>
		public JobQueueStrategy(IJobCollection Jobs, IGraphCollection Graphs, StreamService StreamService, IClock Clock, TimeSpan? LookbackPeriod = null)
		{
			this.Jobs = Jobs;
			this.Graphs = Graphs;
			this.StreamService = StreamService;
			this.Clock = Clock;
			this.LookbackPeriod = LookbackPeriod ?? this.LookbackPeriod;
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
				if (Batch.State != JobStepBatchState.Waiting) continue;

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
			DateTimeOffset MinCreateTime = Clock.UtcNow - LookbackPeriod;
			Dictionary<PoolId, int> PoolQueueSizes = await GetPoolQueueSizesAsync(MinCreateTime);
			List<PoolSizeData> Result = new();
			
			foreach ((PoolId PoolId, int QueueSize) in PoolQueueSizes.OrderBy(x => x.Value))
			{
				PoolSizeData? PoolSize = Pools.Find(x => x.Pool.Id == PoolId);
				if (PoolSize != null)
				{
					// TODO: Tweak these values once in production
					int AdditionalAgentCount = QueueSize switch
					{
						< 10 => 0,
						<= 20 => 1,
						<= 30 => 2,
						<= 40 => 3,
						<= 50 => 4,
						<= 60 => 5,
						_ => 4
					};

					int DesiredAgentCount = PoolSize.Agents.Count + AdditionalAgentCount;
					Result.Add(new(PoolSize.Pool, PoolSize.Agents, DesiredAgentCount, $"QueueSize={QueueSize}"));
				}
			}

			return Result;
		}
	}
}
