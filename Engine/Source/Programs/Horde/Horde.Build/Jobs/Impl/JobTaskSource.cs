// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Common;
using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using HordeCommon.Rpc;
using HordeCommon.Rpc.Tasks;
using HordeServer.Tasks;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using HordeServer.Services;
using EpicGames.Horde.Compute;
using System.Globalization;

namespace HordeServer.Tasks.Impl
{
	using JobId = ObjectId<IJob>;
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Background service to dispatch pending work to agents in priority order.
	/// </summary>
	public sealed class JobTaskSource : TaskSourceBase<ExecuteJobTask>, IDisposable
	{
		/// <inheritdoc/>
		public override string Type => "Job";

		/// <summary>
		/// An item in the queue to be executed
		/// </summary>
		class QueueItem
		{
			/// <summary>
			/// The stream for this job
			/// </summary>
			public IStream Stream;

			/// <summary>
			/// The job instance
			/// </summary>
			public IJob Job;

			/// <summary>
			/// Index of the batch within this job to be executed
			/// </summary>
			public int BatchIdx;

			/// <summary>
			/// The pool of machines to allocate from
			/// </summary>
			public PoolId PoolId;

			/// <summary>
			/// The type of workspace that this item should run in
			/// </summary>
			public AgentWorkspace Workspace;

			/// <summary>
			/// Task for creating a lease and assigning to a waiter
			/// </summary>
			public Task? AssignTask;

			/// <summary>
			/// Accessor for the batch referenced by this item
			/// </summary>
			public IJobStepBatch Batch
			{
				get { return Job.Batches[BatchIdx]; }
			}

			/// <summary>
			/// Returns an identifier describing this unique batch
			/// </summary>
			public (JobId, SubResourceId) Id
			{
				get { return (Job.Id, Batch.Id); }
			}

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Stream">The stream containing this job</param>
			/// <param name="Job">The job instance</param>
			/// <param name="BatchIdx">The batch index to execute</param>
			/// <param name="PoolId">Unique id of the pool of machines to allocate from</param>
			/// <param name="Workspace">The workspace that this job should run in</param>
			public QueueItem(IStream Stream, IJob Job, int BatchIdx, PoolId PoolId, AgentWorkspace Workspace)
			{
				this.Stream = Stream;
				this.Job = Job;
				this.BatchIdx = BatchIdx;
				this.PoolId = PoolId;
				this.Workspace = Workspace;
			}
		}

		/// <summary>
		/// Comparer for items in the queue
		/// </summary>
		class QueueItemComparer : IComparer<QueueItem>
		{
			/// <summary>
			/// Compare two items
			/// </summary>
			/// <param name="X">First item to compare</param>
			/// <param name="Y">Second item to compare</param>
			/// <returns>Negative value if X is a higher priority than Y</returns>
			public int Compare([AllowNull] QueueItem X, [AllowNull] QueueItem Y)
			{
				if (X == null)
				{
					return (Y == null) ? 0 : -1;
				}
				else if (Y == null)
				{
					return 1;
				}

				int Delta = Y.Batch.SchedulePriority - X.Batch.SchedulePriority;
				if (Delta == 0)
				{
					Delta = X.Job.Id.CompareTo(Y.Job.Id);
					if (Delta == 0)
					{
						Delta = (int)X.Batch.Id.Value - (int)Y.Batch.Id.Value;
					}
				}
				return Delta;
			}
		}

		/// <summary>
		/// Information about an agent waiting for work
		/// </summary>
		class QueueWaiter
		{
			/// <summary>
			/// The agent performing the wait
			/// </summary>
			public IAgent Agent { get; }

			/// <summary>
			/// Task to wait for a lease to be assigned
			/// </summary>
			public Task<AgentLease?> Task => LeaseSource.Task;

			/// <summary>
			/// Completion source for the waiting agent. If a new queue item becomes available, the result will be passed through this.
			/// </summary>
			public TaskCompletionSource<AgentLease?> LeaseSource { get; } = new TaskCompletionSource<AgentLease?>();

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Agent">The agent waiting for a task</param>
			public QueueWaiter(IAgent Agent)
			{
				this.Agent = Agent;
			}
		}

		/// <summary>
		/// The database service instance
		/// </summary>
		DatabaseService DatabaseService;

		/// <summary>
		/// The stream service instance
		/// </summary>
		StreamService StreamService;

		/// <summary>
		/// The log file service instance
		/// </summary>
		ILogFileService LogFileService;

		/// <summary>
		/// Collection of agent documents
		/// </summary>
		IAgentCollection AgentsCollection;

		/// <summary>
		/// Collection of job documents
		/// </summary>
		IJobCollection Jobs;

		/// <summary>
		/// Collection of jobstepref documents
		/// </summary>
		IJobStepRefCollection JobStepRefs;

		/// <summary>
		/// Collection of graph documents
		/// </summary>
		IGraphCollection Graphs;

		/// <summary>
		/// Collection of pool documents
		/// </summary>
		IPoolCollection PoolCollection;

		/// <summary>
		/// Collection of UGS metadata documents
		/// </summary>
		IUgsMetadataCollection UgsMetadataCollection;

		/// <summary>
		/// The Perforce load balancer
		/// </summary>
		PerforceLoadBalancer PerforceLoadBalancer;

		/// <summary>
		/// The application lifetime
		/// </summary>
		IHostApplicationLifetime ApplicationLifetime;

		/// <summary>
		/// Settings instance
		/// </summary>
		IOptionsMonitor<ServerSettings> Settings;

		/// <summary>
		/// Log writer
		/// </summary>
		ILogger<JobTaskSource> Logger;

		/// <summary>
		/// Object used for ensuring mutual exclusion to the queues
		/// </summary>
		object LockObject = new object();

		/// <summary>
		/// List of items waiting to be executed
		/// </summary>
		SortedSet<QueueItem> Queue = new SortedSet<QueueItem>(new QueueItemComparer());

		/// <summary>
		/// Map from batch id to the corresponding queue item
		/// </summary>
		Dictionary<(JobId, SubResourceId), QueueItem> BatchIdToQueueItem = new Dictionary<(JobId, SubResourceId), QueueItem>();

		/// <summary>
		/// Set of long-poll tasks waiting to be satisfied 
		/// </summary>
		HashSet<QueueWaiter> Waiters = new HashSet<QueueWaiter>();

		/// <summary>
		/// During a background queue refresh operation, any updated batches are added to this dictionary for merging into the updated queue.
		/// </summary>
		List<QueueItem>? NewQueueItemsDuringUpdate;

		/// <summary>
		/// Cache of pools
		/// </summary>
		Dictionary<PoolId, IPool> CachedPoolIdToInstance = new Dictionary<PoolId, IPool>();

		/// <summary>
		/// Cache of stream objects. Used to resolve agent types.
		/// </summary>
		private Dictionary<StreamId, IStream> Streams = new Dictionary<StreamId, IStream>();

		/// <summary>
		/// Background tick registration
		/// </summary>
		BackgroundTick Ticker;

		/// <summary>
		/// Interval between querying the database for jobs to execute
		/// </summary>
		static readonly TimeSpan RefreshInterval = TimeSpan.FromSeconds(5.0);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="Agents">The agents collection</param>
		/// <param name="Jobs">The jobs collection</param>
		/// <param name="JobStepRefs">Collection of jobstepref documents</param>
		/// <param name="Graphs">The graphs collection</param>
		/// <param name="Pools">The pools collection</param>
		/// <param name="UgsMetadataCollection">Ugs metadata collection</param>
		/// <param name="StreamService">The stream service instance</param>
		/// <param name="LogFileService">The log file service instance</param>
		/// <param name="PerforceLoadBalancer">Perforce load balancer</param>
		/// <param name="ApplicationLifetime">Application lifetime interface</param>
		/// <param name="Settings">Settings for the server</param>
		/// <param name="Logger">Log writer</param>
		public JobTaskSource(DatabaseService DatabaseService, IAgentCollection Agents, IJobCollection Jobs, IJobStepRefCollection JobStepRefs, IGraphCollection Graphs, IPoolCollection Pools, IUgsMetadataCollection UgsMetadataCollection, StreamService StreamService, ILogFileService LogFileService, PerforceLoadBalancer PerforceLoadBalancer, IHostApplicationLifetime ApplicationLifetime, IOptionsMonitor<ServerSettings> Settings, ILogger<JobTaskSource> Logger)
		{
			this.DatabaseService = DatabaseService;
			this.AgentsCollection = Agents;
			this.Jobs = Jobs;
			this.JobStepRefs = JobStepRefs;
			this.Graphs = Graphs;
			this.PoolCollection = Pools;
			this.UgsMetadataCollection = UgsMetadataCollection;
			this.StreamService = StreamService;
			this.LogFileService = LogFileService;
			this.PerforceLoadBalancer = PerforceLoadBalancer;
			this.ApplicationLifetime = ApplicationLifetime;
			this.Ticker = new BackgroundTick(TickAsync, RefreshInterval, Logger);
			this.Settings = Settings;
			this.Logger = Logger;

			OnLeaseStartedProperties.Add(nameof(ExecuteJobTask.JobId), x => new JobId(x.JobId)).Add(nameof(ExecuteJobTask.BatchId), x => SubResourceId.Parse(x.BatchId)).Add(nameof(ExecuteJobTask.LogId), x => new LogId(x.LogId));
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			await Ticker.DisposeAsync();
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Ticker.Dispose();
		}

		/// <summary>
		/// Gets an object containing the stats of the queue for diagnostic purposes.
		/// </summary>
		/// <returns>Status object</returns>
		public object GetStatus()
		{
			lock (LockObject)
			{
				List<object> OutputItems = new List<object>();
				foreach (QueueItem QueueItem in Queue)
				{
					OutputItems.Add(new { JobId = QueueItem.Job.Id.ToString(), BatchId = QueueItem.Batch.Id.ToString(), PoolId = QueueItem.PoolId.ToString(), Workspace = QueueItem.Workspace });
				}

				List<object> OutputWaiters = new List<object>();
				foreach (QueueWaiter Waiter in Waiters)
				{
					OutputWaiters.Add(new { Id = Waiter.Agent.Id.ToString(), Pools = Waiter.Agent.GetPools().Select(x => x.ToString()).ToList(), Workspaces = Waiter.Agent.Workspaces });
				}

				return new { Items = OutputItems, Waiters = OutputWaiters };
			}
		}

		/// <summary>
		/// Cancel any pending wait for an agent, allowing it to cycle its session state immediately
		/// </summary>
		/// <param name="AgentId">The agent id</param>
		public void CancelLongPollForAgent(AgentId AgentId)
		{
			QueueWaiter? Waiter;
			lock (LockObject)
			{
				Waiter = Waiters.FirstOrDefault(x => x.Agent.Id == AgentId);
			}
			if(Waiter != null)
			{
				Waiter.LeaseSource.TrySetResult(null);
			}
		}

		/// <summary>
		/// Background task
		/// </summary>
		/// <param name="StoppingToken">Token that indicates that the service should shut down</param>
		/// <returns>Async task</returns>
		async Task TickAsync(CancellationToken StoppingToken)
		{
			// Set the NewBatchIdToQueueItem member, so we capture any updated jobs during the DB query.
			lock (LockObject)
			{
				NewQueueItemsDuringUpdate = new List<QueueItem>();
			}

			// Query all the current streams
			List<IStream> StreamsList = await StreamService.GetStreamsAsync();
			Streams = StreamsList.ToDictionary(x => x.Id, x => x);

			// Find all the pools which are valid (ie. have at least one online agent)
			DateTime UtcNow = DateTime.UtcNow;
			List<IAgent> Agents = await AgentsCollection.FindAsync();
			List<IPool> Pools = await PoolCollection.GetAsync();

			// Find all the pools which are currently online
			HashSet<PoolId> OnlinePools = new HashSet<PoolId>(Agents.Where(x => x.IsSessionValid(UtcNow) && x.Enabled).SelectMany(x => x.ExplicitPools));
			foreach (IPool Pool in Pools)
			{
				if (Pool.Condition != null && !OnlinePools.Contains(Pool.Id) && Agents.Any(x => x.IsSessionValid(UtcNow) && x.SatisfiesCondition(Pool.Condition) && x.Enabled))
				{
					OnlinePools.Add(Pool.Id);
				}
			}

			// Find lists of valid pools and online pools
			HashSet<PoolId> ValidPools = new HashSet<PoolId>(OnlinePools.Union(Agents.Where(x => !x.IsSessionValid(UtcNow) || !x.Enabled).SelectMany(x => x.ExplicitPools)));
			foreach (IPool Pool in Pools)
			{
				if (Pool.Condition != null && !ValidPools.Contains(Pool.Id) && Agents.Any(x => !x.IsSessionValid(UtcNow) && x.SatisfiesCondition(Pool.Condition) && x.Enabled))
				{
					ValidPools.Add(Pool.Id);
				}
			}

			// Query all the current pools
			CachedPoolIdToInstance = Pools.ToDictionary(x => x.Id, x => x);

			// New list of queue items
			SortedSet<QueueItem> NewQueue = new SortedSet<QueueItem>(Queue.Comparer);
			Dictionary<(JobId, SubResourceId), QueueItem> NewBatchIdToQueueItem = new Dictionary<(JobId, SubResourceId), QueueItem>();

			// Query for a new list of jobs for the queue
			List<IJob> NewJobs = await Jobs.GetDispatchQueueAsync();
			for (int Idx = 0; Idx < NewJobs.Count; Idx++)
			{
				IJob? NewJob = NewJobs[Idx];

				if (NewJob.GraphHash == null)
				{
					Logger.LogError("Job {JobId} has a null graph hash and can't be started.", NewJob.Id);
					await Jobs.TryRemoveFromDispatchQueueAsync(NewJob);
					continue;
				}
				if (NewJob.AbortedByUserId != null)
				{
					Logger.LogError("Job {JobId} was aborted but not removed from dispatch queue", NewJob.Id);
					await Jobs.TryRemoveFromDispatchQueueAsync(NewJob);
					continue;
				}

				// Get the graph for this job
				IGraph Graph = await Graphs.GetAsync(NewJob.GraphHash);

				// Get the stream. If it fails, skip the whole job.
				IStream? Stream;
				if (!Streams.TryGetValue(NewJob.StreamId, out Stream))
				{
					NewJob = await Jobs.SkipAllBatchesAsync(NewJob, Graph, JobStepBatchError.UnknownStream);
					continue;
				}

				// Update all the batches
				HashSet<SubResourceId> CheckedBatchIds = new HashSet<SubResourceId>();
				while (NewJob != null)
				{
					// Find the next batch within this job that is ready
					int BatchIdx = NewJob.Batches.FindIndex(x => x.State == JobStepBatchState.Ready && CheckedBatchIds.Add(x.Id));
					if (BatchIdx == -1)
					{
						break;
					}

					// Validate the agent type and workspace settings
					IJobStepBatch Batch = NewJob.Batches[BatchIdx];
					if (!Stream.AgentTypes.TryGetValue(Graph.Groups[Batch.GroupIdx].AgentType, out AgentType? AgentType))
					{
						NewJob = await SkipBatchAsync(NewJob, Batch.Id, Graph, JobStepBatchError.UnknownAgentType);
					}
					else if (!CachedPoolIdToInstance.TryGetValue(AgentType.Pool, out IPool? Pool))
					{
						NewJob = await SkipBatchAsync(NewJob, Batch.Id, Graph, JobStepBatchError.UnknownPool);
					}
					else if (!ValidPools.Contains(AgentType.Pool))
					{
						NewJob = await SkipBatchAsync(NewJob, Batch.Id, Graph, JobStepBatchError.NoAgentsInPool);
					}
					else if (!OnlinePools.Contains(AgentType.Pool))
					{
						NewJob = await SkipBatchAsync(NewJob, Batch.Id, Graph, JobStepBatchError.NoAgentsOnline);
					}
					else if (!Stream.TryGetAgentWorkspace(AgentType, out AgentWorkspace? Workspace))
					{
						NewJob = await SkipBatchAsync(NewJob, Batch.Id, Graph, JobStepBatchError.UnknownWorkspace);
					}
					else
					{
						QueueItem NewQueueItem = new QueueItem(Stream, NewJob, BatchIdx, AgentType.Pool, Workspace);
						NewQueue.Add(NewQueueItem);
						NewBatchIdToQueueItem[(NewJob.Id, Batch.Id)] = NewQueueItem;
					}
				}

				if (NewJob != null)
				{
					if (!NewJob.Batches.Any(Batch => Batch.State == JobStepBatchState.Ready || Batch.State == JobStepBatchState.Starting || Batch.State == JobStepBatchState.Running || Batch.State == JobStepBatchState.Stopping))
					{
						Logger.LogError("Job {JobId} is in dispatch queue but not currently executing", NewJob.Id);
						await Jobs.TryRemoveFromDispatchQueueAsync(NewJob);
					}
				}
			}

			// Update the queue
			lock (LockObject)
			{
				Queue = NewQueue;
				BatchIdToQueueItem = NewBatchIdToQueueItem;

				// Merge the new queue items with the queue
				foreach (QueueItem NewQueueItem in NewQueueItemsDuringUpdate)
				{
					QueueItem? ExistingQueueItem;
					if (!NewBatchIdToQueueItem.TryGetValue((NewQueueItem.Job.Id, NewQueueItem.Batch.Id), out ExistingQueueItem))
					{
						// Always just add this item
						Queue.Add(NewQueueItem);
						BatchIdToQueueItem[NewQueueItem.Id] = NewQueueItem;
					}
					else if (NewQueueItem.Job.UpdateIndex > ExistingQueueItem.Job.UpdateIndex)
					{
						// Replace the existing item
						Queue.Remove(ExistingQueueItem);
						Queue.Add(NewQueueItem);
						BatchIdToQueueItem[NewQueueItem.Id] = NewQueueItem;
					}
				}

				// Clear out the list to capture queue items during an update
				NewQueueItemsDuringUpdate = null;
			}
		}

		private async Task<IJob?> SkipBatchAsync(IJob Job, SubResourceId BatchId, IGraph Graph, JobStepBatchError Reason)
		{
			Logger.LogInformation("Skipping batch {BatchId} for job {JobId} (reason: {Reason})", BatchId, Job.Id, Reason);

			IReadOnlyList<(LabelState, LabelOutcome)> OldLabelStates = Job.GetLabelStates(Graph);
			IJob? NewJob = await Jobs.SkipBatchAsync(Job, BatchId, Graph, Reason);
			if(NewJob != null)
			{
				IReadOnlyList<(LabelState, LabelOutcome)> NewLabelStates = NewJob.GetLabelStates(Graph);
				await UpdateUgsBadges(NewJob, Graph, OldLabelStates, NewLabelStates);
			}
			return NewJob;
		}

		/// <summary>
		/// Updates the current state of a job
		/// </summary>
		/// <param name="Job">The job that has been updated</param>
		/// <param name="Graph">Graph for the job</param>
		/// <returns>Async task</returns>
		public void UpdateQueuedJob(IJob Job, IGraph Graph)
		{
			IStream? Stream;
			Streams.TryGetValue(Job.StreamId, out Stream);
			UpdateQueuedJob(Job, Graph, Stream);
		}

		void AssignAnyQueueItemToWaiter(QueueWaiter Waiter)
		{
			lock (Waiters)
			{
				foreach(QueueItem Item in BatchIdToQueueItem.Values)
				{
					if (TryAssignItemToWaiter(Item, Waiter))
					{
						break;
					}
				}
			}
		}

		/// <summary>
		/// Attempt to find a waiter that can handle the given queue item
		/// </summary>
		/// <param name="Item">The queue item</param>
		/// <returns></returns>
		void AssignQueueItemToAnyWaiter(QueueItem Item)
		{
			if (Item.AssignTask == null && Item.Batch.SessionId == null)
			{
				lock (Waiters)
				{
					if (Item.AssignTask == null && Item.Batch.SessionId == null)
					{
						foreach (QueueWaiter Waiter in Waiters)
						{
							if (TryAssignItemToWaiter(Item, Waiter))
							{
								break;
							}
						}
					}
				}
			}
		}

		/// <summary>
		/// Attempts to assign a queue item to an agent waiting for work
		/// </summary>
		/// <param name="Item"></param>
		/// <param name="Waiter"></param>
		/// <returns></returns>
		bool TryAssignItemToWaiter(QueueItem Item, QueueWaiter Waiter)
		{
			if (Item.AssignTask == null && Item.Batch.SessionId == null && Waiter.Agent.Enabled && Waiter.Agent.Leases.Count == 0 && Waiter.Agent.IsInPool(Item.PoolId))
			{
				Task StartTask = new Task<Task>(() => TryCreateLeaseAsync(Item, Waiter));
				Task ExecuteTask = StartTask.ContinueWith(Task => Task, TaskScheduler.Default);

				if (Interlocked.CompareExchange(ref Item.AssignTask, ExecuteTask, null) == null)
				{
					StartTask.Start(TaskScheduler.Default);
					return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Updates the current state of a job
		/// </summary>
		/// <param name="Job">The job that has been updated</param>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="Stream">The stream containing the job</param>
		public void UpdateQueuedJob(IJob Job, IGraph Graph, IStream? Stream)
		{
			List<TaskCompletionSource<bool>> CompleteWaiters = new List<TaskCompletionSource<bool>>();
			lock (LockObject)
			{
				for (int BatchIdx = 0; BatchIdx < Job.Batches.Count; BatchIdx++)
				{
					IJobStepBatch Batch = Job.Batches[BatchIdx];
					if (Batch.State == JobStepBatchState.Ready && Stream != null && Batch.AgentId == null)
					{
						// Check if this item is already in the list.
						QueueItem? ExistingItem;
						if (BatchIdToQueueItem.TryGetValue((Job.Id, Batch.Id), out ExistingItem))
						{
							// Make sure this is a newer version of the job. There's no guarantee that this is the latest revision.
							if (Job.UpdateIndex > ExistingItem.Job.UpdateIndex)
							{
								if (Batch.SchedulePriority == ExistingItem.Batch.SchedulePriority)
								{
									ExistingItem.Job = Job;
									ExistingItem.BatchIdx = BatchIdx;
								}
								else
								{
									RemoveQueueItem(ExistingItem);
									InsertQueueItem(Stream, Job, BatchIdx, ExistingItem.PoolId, ExistingItem.Workspace);
								}
							}
							continue;
						}

						// Get the group being executed by this batch
						INodeGroup Group = Graph.Groups[Batch.GroupIdx];

						// Get the requirements for the new queue item
						AgentType? AgentType;
						if (Stream.AgentTypes.TryGetValue(Group.AgentType, out AgentType))
						{
							AgentWorkspace? Workspace;
							if (Stream.TryGetAgentWorkspace(AgentType, out Workspace))
							{
								InsertQueueItem(Stream, Job, BatchIdx, AgentType.Pool, Workspace);
							}
						}
					}
					else
					{
						// Check if this item is already in the list. Remove it if it is.
						QueueItem? ExistingItem;
						if (BatchIdToQueueItem.TryGetValue((Job.Id, Batch.Id), out ExistingItem))
						{
							if (Job.UpdateIndex > ExistingItem.Job.UpdateIndex)
							{
								RemoveQueueItem(ExistingItem);
							}
						}
					}
				}
			}

			// Awake all the threads that have been assigned new work items. Has do be done outside the lock to prevent continuations running within it (see Waiter.CompletionSource for more info).
			foreach (TaskCompletionSource<bool> CompleteWaiter in CompleteWaiters)
			{
				CompleteWaiter.TrySetResult(true);
			}
		}

		/// <inheritdoc/>
		public override async Task<AgentLease?> AssignLeaseAsync(IAgent Agent, CancellationToken CancellationToken)
		{
			QueueWaiter Waiter = new QueueWaiter(Agent);
			lock (LockObject)
			{
				AssignAnyQueueItemToWaiter(Waiter);
				if (Waiter.LeaseSource.Task.IsCompleted)
				{
					return Waiter.Task.Result;
				}
				Waiters.Add(Waiter);
			}

			AgentLease? Lease;
			using (CancellationToken.Register(() => Waiter.LeaseSource.TrySetResult(null)))
			{
				Lease = await Waiter.Task;
			}

			lock (LockObject)
			{
				Waiters.Remove(Waiter);
			}

			return Lease;
		}

		/// <inheritdoc/>
		public override Task CancelLeaseAsync(IAgent Agent, LeaseId LeaseId, ExecuteJobTask Task)
		{
			return CancelLeaseAsync(Agent, new JobId(Task.JobId), Task.BatchId.ToSubResourceId());
		}

		/// <summary>
		/// Assign a new batch to be executed by the given agent
		/// </summary>
		/// <param name="Item">The item to create a lease for</param>
		/// <param name="Waiter">The agent waiting for work</param>
		/// <returns>New work to execute</returns>
		private async Task<AgentLease?> TryCreateLeaseAsync(QueueItem Item, QueueWaiter Waiter)
		{
			IJob Job = Item.Job;
			IJobStepBatch Batch = Item.Batch;
			IAgent Agent = Waiter.Agent;
			Logger.LogDebug("Assigning job {JobId}, batch {BatchId} to waiter (agent {AgentID})", Job.Id, Batch.Id, Agent.Id);

			// Generate a new unique id for the lease
			LeaseId LeaseId = LeaseId.GenerateNewId();

			// The next time to try assigning to another agent
			DateTime BackOffTime = DateTime.UtcNow + TimeSpan.FromMinutes(1.0);

			// Try to update the job with this agent id
			LogId LogId = (await LogFileService.CreateLogFileAsync(Job.Id, Agent.SessionId, LogType.Json)).Id;
			IJob? NewJob = await Jobs.TryAssignLeaseAsync(Item.Job, Item.BatchIdx, Item.PoolId, Agent.Id, Agent.SessionId!.Value, LeaseId, LogId);
			if (NewJob != null)
			{
				Job = NewJob;

				// Get the lease name
				StringBuilder LeaseName = new StringBuilder($"{Item.Stream.Name} - ");
				if (Job.PreflightChange > 0)
				{
					LeaseName.Append((Job.Change > 0) ? $"Preflight CL {Job.PreflightChange} against CL {Job.Change}" : $"Preflight CL {Job.PreflightChange} against latest");
				}
				else
				{
					LeaseName.Append((Job.Change > 0) ? $"CL {Job.Change}" : "Latest CL");
				}
				LeaseName.Append(CultureInfo.InvariantCulture, $" - {Job.Name}");

				// Get the global settings
				Globals Globals = await DatabaseService.GetGlobalsAsync();

				// Encode the payload
				ExecuteJobTask? Task = await CreateExecuteJobTaskAsync(Item.Stream, Job, Batch, Agent, Item.Workspace, LogId);
				if (Task != null)
				{
					byte[] Payload = Any.Pack(Task).ToByteArray();

					// Create the lease and try to set it on the waiter. If this fails, the waiter has already moved on, and the lease can be cancelled.
					AgentLease Lease = new AgentLease(LeaseId, LeaseName.ToString(), Job.StreamId, Item.PoolId, LogId, LeaseState.Pending, null, true, Payload);
					if (Waiter.LeaseSource.TrySetResult(Lease))
					{
						Logger.LogDebug("Assigned lease {LeaseId} to agent {AgentId}", LeaseId, Agent.Id);
						return Lease;
					}
				}

				// Cancel the lease
				Logger.LogDebug("Unable to assign lease {LeaseId} to agent {AgentId}, cancelling", LeaseId, Agent.Id);
				await CancelLeaseAsync(Waiter.Agent, Job.Id, Batch.Id);
			}
			else
			{
				// Unable to assign job
				Logger.LogTrace("Refreshing queue entries for job {JobId}", Job.Id);

				// Get the new copy of the job
				NewJob = await Jobs.GetAsync(Job.Id);
				if (NewJob == null)
				{
					lock (LockObject)
					{
						List<QueueItem> RemoveItems = Queue.Where(x => x.Job == Job).ToList();
						foreach (QueueItem RemoveItem in RemoveItems)
						{
							RemoveQueueItem(RemoveItem);
						}
					}
				}
				else
				{
					IGraph Graph = await Graphs.GetAsync(NewJob.GraphHash);
					UpdateQueuedJob(NewJob, Graph);
				}
			}

			// Clear out the assignment for this item, and try to reassign it
			Item.AssignTask = null;
			AssignQueueItemToAnyWaiter(Item);
			return null;
		}

		async Task<ExecuteJobTask?> CreateExecuteJobTaskAsync(IStream Stream, IJob Job, IJobStepBatch Batch, IAgent Agent, AgentWorkspace Workspace, LogId LogId)
		{
			// Get the lease name
			StringBuilder LeaseName = new StringBuilder($"{Stream.Name} - ");
			if (Job.PreflightChange > 0)
			{
				LeaseName.Append((Job.Change > 0) ? $"Preflight CL {Job.PreflightChange} against CL {Job.Change}" : $"Preflight CL {Job.PreflightChange} against latest");
			}
			else
			{
				LeaseName.Append((Job.Change > 0) ? $"CL {Job.Change}" : "Latest CL");
			}
			LeaseName.Append(CultureInfo.InvariantCulture, $" - {Job.Name}");

			// Get the global settings
			Globals Globals = await DatabaseService.GetGlobalsAsync();

			// Encode the payload
			ExecuteJobTask Task = new ExecuteJobTask();
			Task.JobId = Job.Id.ToString();
			Task.BatchId = Batch.Id.ToString();
			Task.LogId = LogId.ToString();
			Task.JobName = LeaseName.ToString();

			List<HordeCommon.Rpc.Messages.AgentWorkspace> Workspaces = new List<HordeCommon.Rpc.Messages.AgentWorkspace>();

			PerforceCluster? Cluster = Globals.FindPerforceCluster(Workspace.Cluster);
			if (Cluster == null)
			{
				return null;
			}

			AgentWorkspace? AutoSdkWorkspace = Agent.GetAutoSdkWorkspace(Cluster);
			if (AutoSdkWorkspace != null)
			{
				if (!await Agent.TryAddWorkspaceMessage(AutoSdkWorkspace, Cluster, PerforceLoadBalancer, Workspaces))
				{
					return null;
				}
				Task.AutoSdkWorkspace = Workspaces.Last();
			}

			if (!await Agent.TryAddWorkspaceMessage(Workspace, Cluster, PerforceLoadBalancer, Workspaces))
			{
				return null;
			}

			Task.Workspace = Workspaces.Last();
			return Task;
		}

		/// <summary>
		/// Send any badge updates for this job
		/// </summary>
		/// <param name="Job">The job being updated</param>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="OldLabelStates">Previous badge states for the job</param>
		/// <returns>Async task</returns>
		public async Task UpdateUgsBadges(IJob Job, IGraph Graph, IReadOnlyList<(LabelState, LabelOutcome)> OldLabelStates)
		{
			await UpdateUgsBadges(Job, Graph, OldLabelStates, Job.GetLabelStates(Graph));
		}

		/// <summary>
		/// Send any badge updates for this job
		/// </summary>
		/// <param name="Job">The job being updated</param>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="OldLabelStates">Previous badge states for the job</param>
		/// <param name="NewLabelStates">The new badge states for the job</param>
		/// <returns>Async task</returns>
		public async Task UpdateUgsBadges(IJob Job, IGraph Graph, IReadOnlyList<(LabelState, LabelOutcome)> OldLabelStates, IReadOnlyList<(LabelState, LabelOutcome)> NewLabelStates)
		{
			if (!Job.ShowUgsBadges || Job.PreflightChange != 0)
			{
				return;
			}

			IReadOnlyDictionary<int, UgsBadgeState> OldStates = Job.GetUgsBadgeStates(Graph, OldLabelStates);
			IReadOnlyDictionary<int, UgsBadgeState> NewStates = Job.GetUgsBadgeStates(Graph, NewLabelStates);

			// Figure out a list of all the badges that have been modified
			List<int> UpdateLabels = new List<int>();
			foreach (KeyValuePair<int, UgsBadgeState> Pair in OldStates)
			{
				if (!NewStates.ContainsKey(Pair.Key))
				{
					UpdateLabels.Add(Pair.Key);
				}
			}
			foreach (KeyValuePair<int, UgsBadgeState> Pair in NewStates)
			{
				if (!OldStates.TryGetValue(Pair.Key, out UgsBadgeState PrevState) || PrevState != Pair.Value)
				{
					UpdateLabels.Add(Pair.Key);
				}
			}

			// Cached stream for this job
			IStream? Stream = null;

			// Send all the updates
			Dictionary<int, IUgsMetadata> MetadataCache = new Dictionary<int, IUgsMetadata>();
			foreach (int LabelIdx in UpdateLabels)
			{
				ILabel Label = Graph.Labels[LabelIdx];

				// Skip if this label has no UGS name.
				if (Label.UgsName == null)
				{
					continue;
				}

				// Get the new state
				if (!NewStates.TryGetValue(LabelIdx, out UgsBadgeState NewState))
				{
					NewState = UgsBadgeState.Skipped;
				}

				// Get the stream
				if (Stream == null)
				{
					Stream = await StreamService.GetStreamAsync(Job.StreamId);
					if (Stream == null)
					{
						Logger.LogError("Unable to fetch definition for stream {StreamId}", Job.StreamId);
						break;
					}
				}

				// The changelist number to display the badge for
				int Change;
				if (Label.Change == LabelChange.Code)
				{
					Change = Job.CodeChange;
				}
				else
				{
					Change = Job.Change;
				}

				// Get the current metadata state
				IUgsMetadata? Metadata;
				if (!MetadataCache.TryGetValue(Change, out Metadata))
				{
					Metadata = await UgsMetadataCollection.FindOrAddAsync(Stream.Name, Change, Label.UgsProject);
					MetadataCache[Change] = Metadata;
				}

				// Apply the update
				Uri LabelUrl = new Uri(Settings.CurrentValue.DashboardUrl, $"job/{Job.Id}?label={LabelIdx}");
				Logger.LogInformation("Updating state of badge {BadgeName} at {Change} to {NewState} ({LabelUrl})", Label.UgsName, Change, NewState, LabelUrl);
				Metadata = await UgsMetadataCollection.UpdateBadgeAsync(Metadata, Label.UgsName!, LabelUrl, NewState);
				MetadataCache[Change] = Metadata;
			}
		}

		/// <inheritdoc/>
		public override async Task OnLeaseFinishedAsync(IAgent Agent, LeaseId LeaseId, ExecuteJobTask Task, LeaseOutcome Outcome, ReadOnlyMemory<byte> Output, ILogger Logger)
		{
			if (Outcome != LeaseOutcome.Success)
			{
				AgentId AgentId = Agent.Id;
				JobId JobId = new JobId(Task.JobId);
				SubResourceId BatchId = Task.BatchId.ToSubResourceId();

				// Update the batch
				for (; ; )
				{
					IJob? Job = await Jobs.GetAsync(JobId);
					if (Job == null)
					{
						break;
					}

					int BatchIdx = Job.Batches.FindIndex(x => x.Id == BatchId);
					if (BatchIdx == -1)
					{
						break;
					}

					IJobStepBatch Batch = Job.Batches[BatchIdx];
					if (Batch.AgentId != AgentId)
					{
						break;
					}

					int RunningStepIdx = Batch.Steps.FindIndex(x => x.State == JobStepState.Running);

					IGraph Graph = await Graphs.GetAsync(Job.GraphHash);
					Job = await Jobs.TryFailBatchAsync(Job, BatchIdx, Graph);

					if (Job != null)
					{
						if (Batch.Error != JobStepBatchError.None)
						{
							Logger.LogInformation("Failed job {JobId}, batch {BatchId} with error {Error}", Job.Id, Batch.Id, Batch.Error);
						}
						if (RunningStepIdx != -1)
						{
							await JobStepRefs.UpdateAsync(Job, Batch, Batch.Steps[RunningStepIdx], Graph);
						}
						break;
					}
				}
			}
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="Agent"></param>
		/// <param name="JobId"></param>
		/// <param name="BatchId"></param>
		/// <returns></returns>
		async Task CancelLeaseAsync(IAgent Agent, JobId JobId, SubResourceId BatchId)
		{
			Logger.LogDebug("Cancelling lease for job {JobId}, batch {BatchId}", JobId, BatchId);

			// Update the batch
			for (; ; )
			{
				IJob? Job = await Jobs.GetAsync(JobId);
				if (Job == null)
				{
					break;
				}

				int BatchIdx = Job.Batches.FindIndex(x => x.Id == BatchId);
				if (BatchIdx == -1)
				{
					break;
				}

				IJobStepBatch Batch = Job.Batches[BatchIdx];
				if (Batch.AgentId != Agent.Id)
				{
					break;
				}

				IJob? NewJob = await Jobs.TryCancelLeaseAsync(Job, BatchIdx);
				if (NewJob != null)
				{
					break;
				}
			}
		}

		/// <summary>
		/// Inserts an item into the queue
		/// </summary>
		/// <param name="Stream">The stream containing the job</param>
		/// <param name="Job"></param>
		/// <param name="BatchIdx"></param>
		/// <param name="PoolId">The pool to use</param>
		/// <param name="Workspace">The workspace for this item to run in</param>
		/// <returns></returns>
		void InsertQueueItem(IStream Stream, IJob Job, int BatchIdx, PoolId PoolId, AgentWorkspace Workspace)
		{
			Logger.LogDebug("Adding queued job {JobId}, batch {BatchId} [Pool: {Pool}, Workspace: {Workspace}]", Job.Id, Job.Batches[BatchIdx].Id, PoolId, Workspace.Identifier);

			QueueItem NewItem = new QueueItem(Stream, Job, BatchIdx, PoolId, Workspace);
			BatchIdToQueueItem[NewItem.Id] = NewItem;
			Queue.Add(NewItem);

			AssignQueueItemToAnyWaiter(NewItem);
		}

		/// <summary>
		/// Removes an item from the queue
		/// </summary>
		/// <param name="Item">Item to remove</param>
		void RemoveQueueItem(QueueItem Item)
		{
			Logger.LogDebug("Removing queued job {JobId}, batch {BatchId}", Item.Job.Id, Item.Batch.Id);

			Queue.Remove(Item);
			BatchIdToQueueItem.Remove(Item.Id);
		}
	}
}
