// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Options;
using HordeServer.Tasks;
using HordeServer.Utilities;
using HordeServer.Storage;
using StackExchange.Redis;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Bson.Serialization;
using EpicGames.Serialization;
using Microsoft.Extensions.Caching.Memory;
using EpicGames.Horde.Compute;
using Microsoft.Extensions.Hosting;
using System.Threading.Channels;
using EpicGames.Redis;
using HordeServer.Collections;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Common;
using System.Text.Json;

namespace HordeServer.Compute.Impl
{
	using LeaseId = ObjectId<ILease>;

	/// <summary>
	/// Information about a particular task
	/// </summary>
	[RedisConverter(typeof(RedisCbConverter<>))]
	class ComputeTaskInfo
	{
		[CbField("c")]
		public ClusterId ClusterId { get; set; }

		[CbField("h")]
		public RefId TaskRefId { get; set; }

		[CbField("ch")]
		public ChannelId ChannelId { get; set; }

		private ComputeTaskInfo()
		{
		}

		public ComputeTaskInfo(ClusterId ClusterId, RefId TaskRefId, ChannelId ChannelId)
		{
			this.ClusterId = ClusterId;
			this.TaskRefId = TaskRefId;
			this.ChannelId = ChannelId;
		}
	}

	/// <summary>
	/// Current status of a task
	/// </summary>
	[RedisConverter(typeof(RedisCbConverter<>))]
	class ComputeTaskStatus : IComputeTaskStatus
	{
		/// <inheritdoc/>
		[CbField("h")]
		public RefId TaskRefId { get; set; }

		/// <inheritdoc/>
		[CbField("t")]
		public DateTime Time { get; set; }

		/// <inheritdoc/>
		[CbField("s")]
		public ComputeTaskState State { get; set; }

		/// <inheritdoc/>
		[CbField("o")]
		public ComputeTaskOutcome Outcome { get; set; }

		/// <inheritdoc cref="IComputeTaskStatus.AgentId"/>
		[CbField("a")]
		public Utf8String AgentId { get; set; }

		/// <inheritdoc cref="IComputeTaskStatus.LeaseId"/>
		[CbField("l")]
		byte[]? LeaseIdBytes { get; set; }

		/// <inheritdoc/>
		[CbField("r")]
		public RefId? ResultRefId { get; set; }

		/// <inheritdoc/>
		[CbField("d")]
		public string? Detail { get; set; }

		/// <inheritdoc/>
		AgentId? IComputeTaskStatus.AgentId => AgentId.IsEmpty ? (AgentId?)null : new AgentId(AgentId.ToString());

		/// <inheritdoc/>
		public LeaseId? LeaseId
		{
			get => (LeaseIdBytes == null || LeaseIdBytes.Length == 0) ? (LeaseId?)null : new LeaseId(LeaseIdBytes);
			set => LeaseIdBytes = (value != null) ? value.Value.Value.ToByteArray() : null;
		}

		private ComputeTaskStatus()
		{
		}

		public ComputeTaskStatus(RefId TaskRefId, ComputeTaskState State, AgentId? AgentId, LeaseId? LeaseId)
		{
			this.TaskRefId = TaskRefId;
			this.Time = DateTime.UtcNow;
			this.State = State;
			this.AgentId = (AgentId == null) ? Utf8String.Empty : AgentId.Value.ToString();
			this.LeaseIdBytes = (LeaseId != null) ? LeaseId.Value.Value.ToByteArray() : null;
		}
	}

	/// <summary>
	/// Dispatches remote actions. Does not implement any cross-pod communication to satisfy leases; only agents connected to this server instance will be stored.
	/// </summary>
	class ComputeService : TaskSourceBase<ComputeTaskMessage>, IComputeService, IHostedService, IDisposable
	{
		[RedisConverter(typeof(QueueKeySerializer))]
		class QueueKey
		{
			public ClusterId ClusterId { get; set; }
			public IoHash RequirementsHash { get; set; }

			public QueueKey(ClusterId ClusterId, IoHash RequirementsHash)
			{
				this.ClusterId = ClusterId;
				this.RequirementsHash = RequirementsHash;
			}

			public override string ToString() => $"{ClusterId}/{RequirementsHash}";
		}

		class QueueKeySerializer : IRedisConverter<QueueKey>
		{
			public QueueKey FromRedisValue(RedisValue Value)
			{
				string Str = Value.ToString();
				int Idx = Str.LastIndexOf("/", StringComparison.Ordinal);
				return new QueueKey(new ClusterId(Str.Substring(0, Idx)), IoHash.Parse(Str.Substring(Idx + 1)));
			}

			public RedisValue ToRedisValue(QueueKey Value) => $"{Value.ClusterId}/{Value.RequirementsHash}";
		}

		class ClusterInfo : IComputeClusterInfo
		{
			public ClusterId Id { get; set; }
			public NamespaceId NamespaceId { get; set; }
			public BucketId RequestBucketId { get; set; }
			public BucketId ResponseBucketId { get; set; }

			public ClusterInfo(ComputeClusterConfig Config)
			{
				Id = new ClusterId(Config.Id);
				NamespaceId = new NamespaceId(Config.NamespaceId);
				RequestBucketId = new BucketId(Config.RequestBucketId);
				ResponseBucketId = new BucketId(Config.ResponseBucketId);
			}
		}

		/// <inheritdoc/>
		public override string Type => "Compute";

		/// <inheritdoc/>
		public override TaskSourceFlags Flags => TaskSourceFlags.None;

		public static NamespaceId DefaultNamespaceId { get; } = new NamespaceId("default");

		IStorageClient StorageClient;
		ITaskScheduler<QueueKey, ComputeTaskInfo> TaskScheduler;
		RedisMessageQueue<ComputeTaskStatus> MessageQueue;
		ITicker ExpireTasksTicker;
		IMemoryCache RequirementsCache;
		LazyCachedValue<Task<Globals>> Globals;
		LazyCachedValue<Task<List<IPool>>> CachedPools;
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeService(DatabaseService DatabaseService, IDatabase Redis, IStorageClient StorageClient, IPoolCollection PoolCollection, IClock Clock, ILogger<ComputeService> Logger)
		{
			this.StorageClient = StorageClient;
			this.TaskScheduler = new RedisTaskScheduler<QueueKey, ComputeTaskInfo>(Redis, "compute/tasks/", Logger);
			this.MessageQueue = new RedisMessageQueue<ComputeTaskStatus>(Redis, "compute/messages/");
			this.ExpireTasksTicker = Clock.AddTicker(TimeSpan.FromMinutes(2.0), ExpireTasksAsync, Logger);
			this.RequirementsCache = new MemoryCache(new MemoryCacheOptions());
			this.Globals = new LazyCachedValue<Task<Globals>>(() => DatabaseService.GetGlobalsAsync(), TimeSpan.FromSeconds(120.0));
			this.CachedPools = new LazyCachedValue<Task<List<IPool>>>(() => PoolCollection.GetAsync(), TimeSpan.FromSeconds(30.0));
			this.Logger = Logger;

			OnLeaseStartedProperties.Add(x => x.TaskRefId);
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken Token) => ExpireTasksTicker.StartAsync();

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken Token) => ExpireTasksTicker.StopAsync();

		/// <summary>
		/// Expire tasks that are in inactive queues (ie. no machines can execute them)
		/// </summary>
		/// <param name="CancellationToken"></param>
		/// <returns></returns>
		async ValueTask ExpireTasksAsync(CancellationToken CancellationToken)
		{
			List<QueueKey> QueueKeys = await TaskScheduler.GetInactiveQueuesAsync();
			foreach (QueueKey QueueKey in QueueKeys)
			{
				Logger.LogInformation("Inactive queue: {QueueKey}", QueueKey);
				for (; ; )
				{
					ComputeTaskInfo? ComputeTask = await TaskScheduler.DequeueAsync(QueueKey);
					if (ComputeTask == null)
					{
						break;
					}

					ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.TaskRefId, ComputeTaskState.Complete, null, null);
					Status.Outcome = ComputeTaskOutcome.Expired;
					Status.Detail = $"No agents monitoring queue {QueueKey}";
					Logger.LogInformation("Compute task expired (queue: {RequirementsHash}, task: {TaskHash}, channel: {ChannelId})", QueueKey, ComputeTask.TaskRefId, ComputeTask.ChannelId);
					await PostStatusMessageAsync(ComputeTask, Status);
				}
			}
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			MessageQueue.Dispose();
			ExpireTasksTicker.Dispose();
			RequirementsCache.Dispose();
		}

		/// <summary>
		/// Gets information about a compute cluster
		/// </summary>
		/// <param name="ClusterId">Cluster to use for execution</param>
		public async Task<IComputeClusterInfo> GetClusterInfoAsync(ClusterId ClusterId)
		{
			ComputeClusterConfig? Config = await GetClusterAsync(ClusterId);
			if (Config == null)
			{
				throw new KeyNotFoundException();
			}
			return new ClusterInfo(Config);
		}

		/// <inheritdoc/>
		public async Task AddTasksAsync(ClusterId ClusterId, ChannelId ChannelId, List<RefId> TaskRefIds, CbObjectAttachment RequirementsHash)
		{
			List<Task> Tasks = new List<Task>();
			foreach (RefId TaskRefId in TaskRefIds)
			{
				ComputeTaskInfo TaskInfo = new ComputeTaskInfo(ClusterId, TaskRefId, ChannelId);
				Logger.LogDebug("Adding task {TaskHash} from channel {ChannelId} to queue {ClusterId}{RequirementsHash}", TaskRefId.Hash, ChannelId, ClusterId, RequirementsHash);
				Tasks.Add(TaskScheduler.EnqueueAsync(new QueueKey(ClusterId, RequirementsHash), TaskInfo, false));
			}
			await Task.WhenAll(Tasks);
		}

		async ValueTask<ComputeClusterConfig?> GetClusterAsync(ClusterId ClusterId)
		{
			Globals GlobalsInstance = await Globals.GetCached();
			return GlobalsInstance.ComputeClusters.FirstOrDefault(x => new ClusterId(x.Id) == ClusterId);
		}

		/// <inheritdoc/>
		public override async Task<AgentLease?> AssignLeaseAsync(IAgent Agent, CancellationToken CancellationToken)
		{
			// If the agent is disabled, just block until we cancel
			if (!Agent.Enabled)
			{
				using CancellationTask Task = new CancellationTask(CancellationToken);
				await Task.Task;
				return null;
			}

			// Find a task to execute
			(QueueKey, ComputeTaskInfo)? Entry = await TaskScheduler.DequeueAsync(QueueKey => CheckRequirements(Agent, QueueKey), CancellationToken);
			if (Entry != null)
			{
				(QueueKey QueueKey, ComputeTaskInfo TaskInfo) = Entry.Value;

				ComputeClusterConfig? Cluster = await GetClusterAsync(TaskInfo.ClusterId);
				if (Cluster == null)
				{
					Logger.LogWarning("Invalid cluster '{ClusterId}'; failing task {TaskRefId}", TaskInfo.ClusterId, TaskInfo.TaskRefId);
					ComputeTaskStatus Status = new ComputeTaskStatus(TaskInfo.TaskRefId, ComputeTaskState.Complete, Agent.Id, null) { Detail = $"Invalid cluster '{TaskInfo.ClusterId}'" };
					await PostStatusMessageAsync(TaskInfo, Status);
					return null;
				}

				Requirements? Requirements = await GetCachedRequirementsAsync(QueueKey);
				if (Requirements == null)
				{
					Logger.LogWarning("Unable to fetch requirements {RequirementsHash}", QueueKey);
					ComputeTaskStatus Status = new ComputeTaskStatus(TaskInfo.TaskRefId, ComputeTaskState.Complete, Agent.Id, null) { Detail = $"Unable to retrieve requirements '{QueueKey}'" };
					await PostStatusMessageAsync(TaskInfo, Status);
					return null;
				}

				ComputeTaskMessage ComputeTask = new ComputeTaskMessage();
				ComputeTask.ClusterId = TaskInfo.ClusterId.ToString();
				ComputeTask.ChannelId = TaskInfo.ChannelId.ToString();
				ComputeTask.NamespaceId = Cluster.NamespaceId.ToString();
				ComputeTask.InputBucketId = Cluster.RequestBucketId.ToString();
				ComputeTask.OutputBucketId = Cluster.ResponseBucketId.ToString();
				ComputeTask.RequirementsHash = QueueKey.RequirementsHash;
				ComputeTask.TaskRefId = TaskInfo.TaskRefId;

				string LeaseName = $"Remote action ({TaskInfo.TaskRefId})";
				byte[] Payload = Any.Pack(ComputeTask).ToByteArray();

				AgentLease Lease = new AgentLease(LeaseId.GenerateNewId(), LeaseName, null, null, null, LeaseState.Pending, Requirements.Resources, Requirements.Exclusive, Payload);
				Logger.LogDebug("Created lease {LeaseId} for channel {ChannelId} task {TaskHash} req {RequirementsHash}", Lease.Id, ComputeTask.ChannelId, ComputeTask.TaskRefId, ComputeTask.RequirementsHash);
				return Lease;
			}
			return null;
		}

		/// <inheritdoc/>
		public override Task CancelLeaseAsync(IAgent Agent, LeaseId LeaseId, ComputeTaskMessage Message)
		{
			ClusterId ClusterId = new ClusterId(Message.ClusterId);
			ComputeTaskInfo TaskInfo = new ComputeTaskInfo(ClusterId, new RefId(new IoHash(Message.TaskRefId.ToByteArray())), new ChannelId(Message.ChannelId));
			return TaskScheduler.EnqueueAsync(new QueueKey(ClusterId, new IoHash(Message.RequirementsHash.ToByteArray())), TaskInfo, true);
		}

		/// <inheritdoc/>
		public async Task<List<IComputeTaskStatus>> GetTaskUpdatesAsync(ClusterId ClusterId, ChannelId ChannelId)
		{
			List<ComputeTaskStatus> Messages = await MessageQueue.ReadMessagesAsync(GetMessageQueueId(ClusterId, ChannelId));
			return Messages.ConvertAll<IComputeTaskStatus>(x => x);
		}

		public async Task<List<IComputeTaskStatus>> WaitForTaskUpdatesAsync(ClusterId ClusterId, ChannelId ChannelId, CancellationToken CancellationToken)
		{
			List<ComputeTaskStatus> Messages = await MessageQueue.WaitForMessagesAsync(GetMessageQueueId(ClusterId, ChannelId), CancellationToken);
			return Messages.ConvertAll<IComputeTaskStatus>(x => x);
		}

		public override async Task OnLeaseStartedAsync(IAgent Agent, LeaseId LeaseId, ComputeTaskMessage ComputeTask, ILogger Logger)
		{
			await base.OnLeaseStartedAsync(Agent, LeaseId, ComputeTask, Logger);

			ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.TaskRefId, ComputeTaskState.Executing, Agent.Id, LeaseId);
			await PostStatusMessageAsync(ComputeTask, Status);
		}

		public override async Task OnLeaseFinishedAsync(IAgent Agent, LeaseId LeaseId, ComputeTaskMessage ComputeTask, LeaseOutcome Outcome, ReadOnlyMemory<byte> Output, ILogger Logger)
		{
			await base.OnLeaseFinishedAsync(Agent, LeaseId, ComputeTask, Outcome, Output, Logger);

			ComputeTaskResultMessage Message = ComputeTaskResultMessage.Parser.ParseFrom(Output.ToArray());

			ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.TaskRefId, ComputeTaskState.Complete, Agent.Id, LeaseId);
			if (Message.ResultRefId != null)
			{
				Status.ResultRefId = Message.ResultRefId;
			}
			else if ((ComputeTaskOutcome)Message.Outcome != ComputeTaskOutcome.Success)
			{
				(Status.Outcome, Status.Detail) = ((ComputeTaskOutcome)Message.Outcome, Message.Detail);
			}
			else if (Outcome == LeaseOutcome.Failed)
			{
				Status.Outcome = ComputeTaskOutcome.Failed;
			}
			else if (Outcome == LeaseOutcome.Cancelled)
			{
				Status.Outcome = ComputeTaskOutcome.Cancelled;
			}
			else
			{
				Status.Outcome = ComputeTaskOutcome.NoResult;
			}

			Logger.LogInformation("Compute lease finished (lease: {LeaseId}, task: {TaskHash}, agent: {AgentId}, channel: {ChannelId}, result: {ResultHash}, outcome: {Outcome})", LeaseId, ComputeTask.TaskRefId.AsRefId(), Agent.Id, ComputeTask.ChannelId, Status.ResultRefId?.ToString() ?? "(none)", Status.Outcome);
			await PostStatusMessageAsync(ComputeTask, Status);
		}

		/// <summary>
		/// Checks that an agent matches the necessary criteria to execute a task
		/// </summary>
		/// <param name="Agent"></param>
		/// <param name="QueueKey"></param>
		/// <returns></returns>
		async ValueTask<bool> CheckRequirements(IAgent Agent, QueueKey QueueKey)
		{
			Requirements? Requirements = await GetCachedRequirementsAsync(QueueKey);
			if (Requirements == null)
			{
				return false;
			}
			return Agent.MeetsRequirements(Requirements);
		}

		/// <summary>
		/// Gets the requirements object from the CAS
		/// </summary>
		/// <param name="QueueKey">Queue identifier</param>
		/// <returns>Requirements object for the queue</returns>
		async ValueTask<Requirements?> GetCachedRequirementsAsync(QueueKey QueueKey)
		{
			Requirements? Requirements;
			if (!RequirementsCache.TryGetValue(QueueKey.RequirementsHash, out Requirements))
			{
				Requirements = await GetRequirementsAsync(QueueKey);
				if (Requirements != null)
				{
					using (ICacheEntry Entry = RequirementsCache.CreateEntry(QueueKey.RequirementsHash))
					{
						Entry.SetSlidingExpiration(TimeSpan.FromMinutes(10.0));
						Entry.SetValue(Requirements);
					}
				}
			}
			return Requirements;
		}

		/// <summary>
		/// Gets the requirements object for a given queue. Fails tasks in the queue if the requirements object is missing.
		/// </summary>
		/// <param name="QueueKey">Queue identifier</param>
		/// <returns>Requirements object for the queue</returns>
		async ValueTask<Requirements?> GetRequirementsAsync(QueueKey QueueKey)
		{
			Requirements? Requirements = null;

			ComputeClusterConfig? ClusterConfig = await GetClusterAsync(QueueKey.ClusterId);
			if (ClusterConfig != null)
			{
				NamespaceId NamespaceId = new NamespaceId(ClusterConfig.NamespaceId);
				try
				{
					Requirements = await StorageClient.ReadObjectAsync<Requirements>(NamespaceId, QueueKey.RequirementsHash);
				}
				catch (BlobNotFoundException)
				{
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Unable to read blob {NamespaceId}/{RequirementsHash} from storage service", ClusterConfig.NamespaceId, QueueKey.RequirementsHash);
				}
			}

			if (Requirements == null)
			{
				Logger.LogWarning("Unable to fetch requirements object for queue {QueueKey}; failing queued tasks.", QueueKey);
				for (; ; )
				{
					ComputeTaskInfo? ComputeTask = await TaskScheduler.DequeueAsync(QueueKey);
					if (ComputeTask == null)
					{
						break;
					}

					ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.TaskRefId, ComputeTaskState.Complete, null, null);
					Status.Outcome = ComputeTaskOutcome.BlobNotFound;
					Status.Detail = $"Missing requirements object {QueueKey.RequirementsHash}";
					Logger.LogInformation("Compute task failed due to missing requirements (queue: {QueueKey}, task: {TaskHash}, channel: {ChannelId})", QueueKey, ComputeTask.TaskRefId, ComputeTask.ChannelId);
					await PostStatusMessageAsync(ComputeTask, Status);
				}
			}

			return Requirements;
		}

		/// <summary>
		/// Post a status message for a particular task
		/// </summary>
		/// <param name="ComputeTask">The compute task instance</param>
		/// <param name="Status">New status for the task</param>
		async Task PostStatusMessageAsync(ComputeTaskInfo ComputeTask, ComputeTaskStatus Status)
		{
			await MessageQueue.PostAsync(GetMessageQueueId(ComputeTask.ClusterId, ComputeTask.ChannelId), Status);
		}

		/// <summary>
		/// Post a status message for a particular task
		/// </summary>
		/// <param name="ComputeTaskMessage">The compute task lease</param>
		/// <param name="Status">New status for the task</param>
		/// <returns></returns>
		async Task PostStatusMessageAsync(ComputeTaskMessage ComputeTaskMessage, ComputeTaskStatus Status)
		{
			await MessageQueue.PostAsync(GetMessageQueueId(new ClusterId(ComputeTaskMessage.ClusterId), new ChannelId(ComputeTaskMessage.ChannelId)), Status);
		}

		/// <summary>
		/// Gets the name of a particular message queue
		/// </summary>
		/// <param name="ClusterId">The compute cluster</param>
		/// <param name="ChannelId">Identifier for the message channel</param>
		/// <returns>Name of the message queue</returns>
		static string GetMessageQueueId(ClusterId ClusterId, ChannelId ChannelId)
		{
			return $"{ClusterId}/{ChannelId}";
		}
	}
}
