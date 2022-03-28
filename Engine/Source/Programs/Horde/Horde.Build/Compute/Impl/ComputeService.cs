// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Storage;
using EpicGames.Redis;
using EpicGames.Serialization;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Horde.Build.Models;
using Horde.Build.Services;
using Horde.Build.Tasks;
using Horde.Build.Utilities;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using StackExchange.Redis;

namespace Horde.Build.Compute.Impl
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

		public ComputeTaskInfo(ClusterId clusterId, RefId taskRefId, ChannelId channelId)
		{
			ClusterId = clusterId;
			TaskRefId = taskRefId;
			ChannelId = channelId;
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
			set => LeaseIdBytes = value?.Value.ToByteArray();
		}

		private ComputeTaskStatus()
		{
		}

		public ComputeTaskStatus(RefId taskRefId, ComputeTaskState state, AgentId? agentId, LeaseId? leaseId)
		{
			TaskRefId = taskRefId;
			Time = DateTime.UtcNow;
			State = state;
			AgentId = (agentId == null) ? Utf8String.Empty : agentId.Value.ToString();
			LeaseIdBytes = leaseId?.Value.ToByteArray();
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

			public QueueKey(ClusterId clusterId, IoHash requirementsHash)
			{
				ClusterId = clusterId;
				RequirementsHash = requirementsHash;
			}

			public override string ToString() => $"{ClusterId}/{RequirementsHash}";
		}

		class QueueKeySerializer : IRedisConverter<QueueKey>
		{
			public QueueKey FromRedisValue(RedisValue value)
			{
				string str = value.ToString();
				int idx = str.LastIndexOf("/", StringComparison.Ordinal);
				return new QueueKey(new ClusterId(str.Substring(0, idx)), IoHash.Parse(str.Substring(idx + 1)));
			}

			public RedisValue ToRedisValue(QueueKey value) => $"{value.ClusterId}/{value.RequirementsHash}";
		}

		class ClusterInfo : IComputeClusterInfo
		{
			public ClusterId Id { get; set; }
			public NamespaceId NamespaceId { get; set; }
			public BucketId RequestBucketId { get; set; }
			public BucketId ResponseBucketId { get; set; }

			public ClusterInfo(ComputeClusterConfig config)
			{
				Id = new ClusterId(config.Id);
				NamespaceId = new NamespaceId(config.NamespaceId);
				RequestBucketId = new BucketId(config.RequestBucketId);
				ResponseBucketId = new BucketId(config.ResponseBucketId);
			}
		}

		/// <inheritdoc/>
		public override string Type => "Compute";

		/// <inheritdoc/>
		public override TaskSourceFlags Flags => TaskSourceFlags.None;

		public static NamespaceId DefaultNamespaceId { get; } = new NamespaceId("default");

		readonly IStorageClient _storageClient;
		readonly ITaskScheduler<QueueKey, ComputeTaskInfo> _taskScheduler;
		readonly RedisMessageQueue<ComputeTaskStatus> _messageQueue;
		readonly ITicker _expireTasksTicker;
		readonly IMemoryCache _requirementsCache;
		readonly LazyCachedValue<Task<Globals>> _globals;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeService(DatabaseService databaseService, IDatabase redis, IStorageClient storageClient, IClock clock, ILogger<ComputeService> logger)
		{
			_storageClient = storageClient;
			_taskScheduler = new RedisTaskScheduler<QueueKey, ComputeTaskInfo>(redis, "compute/tasks/", logger);
			_messageQueue = new RedisMessageQueue<ComputeTaskStatus>(redis, "compute/messages/");
			_expireTasksTicker = clock.AddTicker<ComputeService>(TimeSpan.FromMinutes(2.0), ExpireTasksAsync, logger);
			_requirementsCache = new MemoryCache(new MemoryCacheOptions());
			_globals = new LazyCachedValue<Task<Globals>>(() => databaseService.GetGlobalsAsync(), TimeSpan.FromSeconds(120.0));
			_logger = logger;

			OnLeaseStartedProperties.Add(x => x.TaskRefId);
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken token) => _expireTasksTicker.StartAsync();

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken token) => _expireTasksTicker.StopAsync();

		/// <summary>
		/// Expire tasks that are in inactive queues (ie. no machines can execute them)
		/// </summary>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		async ValueTask ExpireTasksAsync(CancellationToken cancellationToken)
		{
			List<QueueKey> queueKeys = await _taskScheduler.GetInactiveQueuesAsync();
			foreach (QueueKey queueKey in queueKeys)
			{
				_logger.LogInformation("Inactive queue: {QueueKey}", queueKey);
				for (; ; )
				{
					ComputeTaskInfo? computeTask = await _taskScheduler.DequeueAsync(queueKey);
					if (computeTask == null)
					{
						break;
					}

					ComputeTaskStatus status = new ComputeTaskStatus(computeTask.TaskRefId, ComputeTaskState.Complete, null, null);
					status.Outcome = ComputeTaskOutcome.Expired;
					status.Detail = $"No agents monitoring queue {queueKey}";
					_logger.LogInformation("Compute task expired (queue: {RequirementsHash}, task: {TaskHash}, channel: {ChannelId})", queueKey, computeTask.TaskRefId, computeTask.ChannelId);
					await PostStatusMessageAsync(computeTask, status);
				}
			}
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_messageQueue.Dispose();
			_expireTasksTicker.Dispose();
			_requirementsCache.Dispose();
		}

		/// <summary>
		/// Gets information about a compute cluster
		/// </summary>
		/// <param name="clusterId">Cluster to use for execution</param>
		public async Task<IComputeClusterInfo> GetClusterInfoAsync(ClusterId clusterId)
		{
			ComputeClusterConfig? config = await GetClusterAsync(clusterId);
			if (config == null)
			{
				throw new KeyNotFoundException();
			}
			return new ClusterInfo(config);
		}

		/// <inheritdoc/>
		public async Task AddTasksAsync(ClusterId clusterId, ChannelId channelId, List<RefId> taskRefIds, CbObjectAttachment requirementsHash)
		{
			List<Task> tasks = new List<Task>();
			foreach (RefId taskRefId in taskRefIds)
			{
				ComputeTaskInfo taskInfo = new ComputeTaskInfo(clusterId, taskRefId, channelId);
				_logger.LogDebug("Adding task {TaskHash} from channel {ChannelId} to queue {ClusterId}{RequirementsHash}", taskRefId.Hash, channelId, clusterId, requirementsHash);
				tasks.Add(_taskScheduler.EnqueueAsync(new QueueKey(clusterId, requirementsHash), taskInfo, false));
			}
			await Task.WhenAll(tasks);
		}

		async ValueTask<ComputeClusterConfig?> GetClusterAsync(ClusterId clusterId)
		{
			Globals globalsInstance = await _globals.GetCached();
			return globalsInstance.ComputeClusters.FirstOrDefault(x => new ClusterId(x.Id) == clusterId);
		}

		/// <inheritdoc/>
		public override async Task<Task<AgentLease>> AssignLeaseAsync(IAgent agent, CancellationToken cancellationToken)
		{
			Task<(QueueKey, ComputeTaskInfo)> task = await _taskScheduler.DequeueAsync(queueKey => CheckRequirements(agent, queueKey), cancellationToken);
			return WaitForLeaseAsync(agent, task, cancellationToken);
		}

		private async Task<AgentLease> WaitForLeaseAsync(IAgent agent, Task<(QueueKey, ComputeTaskInfo)> task, CancellationToken cancellationToken)
		{
			for (; ; )
			{
				(QueueKey, ComputeTaskInfo) entry = await task;

				AgentLease? lease = await CreateLeaseForEntryAsync(agent, await task);
				if (lease != null)
				{
					return lease;
				}

				task = await _taskScheduler.DequeueAsync(queueKey => CheckRequirements(agent, queueKey), cancellationToken);
			}
		}

		private async Task<AgentLease?> CreateLeaseForEntryAsync(IAgent agent, (QueueKey, ComputeTaskInfo) entry)
		{
			(QueueKey queueKey, ComputeTaskInfo taskInfo) = entry;

			ComputeClusterConfig? cluster = await GetClusterAsync(taskInfo.ClusterId);
			if (cluster == null)
			{
				_logger.LogWarning("Invalid cluster '{ClusterId}'; failing task {TaskRefId}", taskInfo.ClusterId, taskInfo.TaskRefId);
				ComputeTaskStatus status = new ComputeTaskStatus(taskInfo.TaskRefId, ComputeTaskState.Complete, agent.Id, null) { Detail = $"Invalid cluster '{taskInfo.ClusterId}'" };
				await PostStatusMessageAsync(taskInfo, status);
				return null;
			}

			Requirements? requirements = await GetCachedRequirementsAsync(queueKey);
			if (requirements == null)
			{
				_logger.LogWarning("Unable to fetch requirements {RequirementsHash}", queueKey);
				ComputeTaskStatus status = new ComputeTaskStatus(taskInfo.TaskRefId, ComputeTaskState.Complete, agent.Id, null) { Detail = $"Unable to retrieve requirements '{queueKey}'" };
				await PostStatusMessageAsync(taskInfo, status);
				return null;
			}

			ComputeTaskMessage computeTask = new ComputeTaskMessage();
			computeTask.ClusterId = taskInfo.ClusterId.ToString();
			computeTask.ChannelId = taskInfo.ChannelId.ToString();
			computeTask.NamespaceId = cluster.NamespaceId.ToString();
			computeTask.InputBucketId = cluster.RequestBucketId.ToString();
			computeTask.OutputBucketId = cluster.ResponseBucketId.ToString();
			computeTask.RequirementsHash = queueKey.RequirementsHash;
			computeTask.TaskRefId = taskInfo.TaskRefId;

			string leaseName = $"Remote action ({taskInfo.TaskRefId})";
			byte[] payload = Any.Pack(computeTask).ToByteArray();

			AgentLease lease = new AgentLease(LeaseId.GenerateNewId(), leaseName, null, null, null, LeaseState.Pending, requirements.Resources, requirements.Exclusive, payload);
			_logger.LogDebug("Created lease {LeaseId} for channel {ChannelId} task {TaskHash} req {RequirementsHash}", lease.Id, computeTask.ChannelId, computeTask.TaskRefId, computeTask.RequirementsHash);
			return lease;
		}

		/// <inheritdoc/>
		public override Task CancelLeaseAsync(IAgent agent, LeaseId leaseId, ComputeTaskMessage message)
		{
			ClusterId clusterId = new ClusterId(message.ClusterId);
			ComputeTaskInfo taskInfo = new ComputeTaskInfo(clusterId, new RefId(new IoHash(message.TaskRefId.ToByteArray())), new ChannelId(message.ChannelId));
			return _taskScheduler.EnqueueAsync(new QueueKey(clusterId, new IoHash(message.RequirementsHash.ToByteArray())), taskInfo, true);
		}

		/// <inheritdoc/>
		public async Task<List<IComputeTaskStatus>> GetTaskUpdatesAsync(ClusterId clusterId, ChannelId channelId)
		{
			List<ComputeTaskStatus> messages = await _messageQueue.ReadMessagesAsync(GetMessageQueueId(clusterId, channelId));
			return messages.ConvertAll<IComputeTaskStatus>(x => x);
		}

		public async Task<List<IComputeTaskStatus>> WaitForTaskUpdatesAsync(ClusterId clusterId, ChannelId channelId, CancellationToken cancellationToken)
		{
			List<ComputeTaskStatus> messages = await _messageQueue.WaitForMessagesAsync(GetMessageQueueId(clusterId, channelId), cancellationToken);
			return messages.ConvertAll<IComputeTaskStatus>(x => x);
		}

		public override async Task OnLeaseStartedAsync(IAgent agent, LeaseId leaseId, ComputeTaskMessage computeTask, ILogger logger)
		{
			await base.OnLeaseStartedAsync(agent, leaseId, computeTask, logger);

			ComputeTaskStatus status = new ComputeTaskStatus(computeTask.TaskRefId, ComputeTaskState.Executing, agent.Id, leaseId);
			await PostStatusMessageAsync(computeTask, status);
		}

		public override async Task OnLeaseFinishedAsync(IAgent agent, LeaseId leaseId, ComputeTaskMessage computeTask, LeaseOutcome outcome, ReadOnlyMemory<byte> output, ILogger logger)
		{
			await base.OnLeaseFinishedAsync(agent, leaseId, computeTask, outcome, output, logger);

			ComputeTaskResultMessage message = ComputeTaskResultMessage.Parser.ParseFrom(output.ToArray());

			ComputeTaskStatus status = new ComputeTaskStatus(computeTask.TaskRefId, ComputeTaskState.Complete, agent.Id, leaseId);
			if (message.ResultRefId != null)
			{
				status.ResultRefId = message.ResultRefId;
			}
			else if ((ComputeTaskOutcome)message.Outcome != ComputeTaskOutcome.Success)
			{
				(status.Outcome, status.Detail) = ((ComputeTaskOutcome)message.Outcome, message.Detail);
			}
			else if (outcome == LeaseOutcome.Failed)
			{
				status.Outcome = ComputeTaskOutcome.Failed;
			}
			else if (outcome == LeaseOutcome.Cancelled)
			{
				status.Outcome = ComputeTaskOutcome.Cancelled;
			}
			else
			{
				status.Outcome = ComputeTaskOutcome.NoResult;
			}

			logger.LogInformation("Compute lease finished (lease: {LeaseId}, task: {TaskHash}, agent: {AgentId}, channel: {ChannelId}, result: {ResultHash}, outcome: {Outcome})", leaseId, computeTask.TaskRefId.AsRefId(), agent.Id, computeTask.ChannelId, status.ResultRefId?.ToString() ?? "(none)", status.Outcome);
			await PostStatusMessageAsync(computeTask, status);
		}

		/// <summary>
		/// Checks that an agent matches the necessary criteria to execute a task
		/// </summary>
		/// <param name="agent"></param>
		/// <param name="queueKey"></param>
		/// <returns></returns>
		async ValueTask<bool> CheckRequirements(IAgent agent, QueueKey queueKey)
		{
			Requirements? requirements = await GetCachedRequirementsAsync(queueKey);
			if (requirements == null)
			{
				return false;
			}
			return agent.MeetsRequirements(requirements);
		}

		/// <summary>
		/// Gets the requirements object from the CAS
		/// </summary>
		/// <param name="queueKey">Queue identifier</param>
		/// <returns>Requirements object for the queue</returns>
		async ValueTask<Requirements?> GetCachedRequirementsAsync(QueueKey queueKey)
		{
			Requirements? requirements;
			if (!_requirementsCache.TryGetValue(queueKey.RequirementsHash, out requirements))
			{
				requirements = await GetRequirementsAsync(queueKey);
				if (requirements != null)
				{
					using (ICacheEntry entry = _requirementsCache.CreateEntry(queueKey.RequirementsHash))
					{
						entry.SetSlidingExpiration(TimeSpan.FromMinutes(10.0));
						entry.SetValue(requirements);
					}
				}
			}
			return requirements;
		}

		/// <summary>
		/// Gets the requirements object for a given queue. Fails tasks in the queue if the requirements object is missing.
		/// </summary>
		/// <param name="queueKey">Queue identifier</param>
		/// <returns>Requirements object for the queue</returns>
		async ValueTask<Requirements?> GetRequirementsAsync(QueueKey queueKey)
		{
			Requirements? requirements = null;

			ComputeClusterConfig? clusterConfig = await GetClusterAsync(queueKey.ClusterId);
			if (clusterConfig != null)
			{
				NamespaceId namespaceId = new NamespaceId(clusterConfig.NamespaceId);
				try
				{
					requirements = await _storageClient.ReadBlobAsync<Requirements>(namespaceId, queueKey.RequirementsHash);
				}
				catch (BlobNotFoundException)
				{
				}
				catch (Exception ex)
				{
					_logger.LogError(ex, "Unable to read blob {NamespaceId}/{RequirementsHash} from storage service", clusterConfig.NamespaceId, queueKey.RequirementsHash);
				}
			}

			if (requirements == null)
			{
				_logger.LogWarning("Unable to fetch requirements object for queue {QueueKey}; failing queued tasks.", queueKey);
				for (; ; )
				{
					ComputeTaskInfo? computeTask = await _taskScheduler.DequeueAsync(queueKey);
					if (computeTask == null)
					{
						break;
					}

					ComputeTaskStatus status = new ComputeTaskStatus(computeTask.TaskRefId, ComputeTaskState.Complete, null, null);
					status.Outcome = ComputeTaskOutcome.BlobNotFound;
					status.Detail = $"Missing requirements object {queueKey.RequirementsHash}";
					_logger.LogInformation("Compute task failed due to missing requirements (queue: {QueueKey}, task: {TaskHash}, channel: {ChannelId})", queueKey, computeTask.TaskRefId, computeTask.ChannelId);
					await PostStatusMessageAsync(computeTask, status);
				}
			}

			return requirements;
		}

		/// <summary>
		/// Post a status message for a particular task
		/// </summary>
		/// <param name="computeTask">The compute task instance</param>
		/// <param name="status">New status for the task</param>
		async Task PostStatusMessageAsync(ComputeTaskInfo computeTask, ComputeTaskStatus status)
		{
			await _messageQueue.PostAsync(GetMessageQueueId(computeTask.ClusterId, computeTask.ChannelId), status);
		}

		/// <summary>
		/// Post a status message for a particular task
		/// </summary>
		/// <param name="computeTaskMessage">The compute task lease</param>
		/// <param name="status">New status for the task</param>
		/// <returns></returns>
		async Task PostStatusMessageAsync(ComputeTaskMessage computeTaskMessage, ComputeTaskStatus status)
		{
			await _messageQueue.PostAsync(GetMessageQueueId(new ClusterId(computeTaskMessage.ClusterId), new ChannelId(computeTaskMessage.ChannelId)), status);
		}

		/// <summary>
		/// Gets the name of a particular message queue
		/// </summary>
		/// <param name="clusterId">The compute cluster</param>
		/// <param name="channelId">Identifier for the message channel</param>
		/// <returns>Name of the message queue</returns>
		static string GetMessageQueueId(ClusterId clusterId, ChannelId channelId)
		{
			return $"{clusterId}/{channelId}";
		}
	}
}
