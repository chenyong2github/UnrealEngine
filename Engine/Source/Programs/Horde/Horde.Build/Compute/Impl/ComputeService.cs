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

namespace HordeServer.Compute.Impl
{
	using ChannelId = StringId<IComputeChannel>;
	using LeaseId = ObjectId<ILease>;
	using NamespaceId = StringId<INamespace>;

	/// <summary>
	/// Information about a particular task
	/// </summary>
	[RedisConverter(typeof(RedisCbConverter<>))]
	class ComputeTaskInfo
	{
		[CbField("h")]
		public CbObjectAttachment TaskHash { get; set; }

		[CbField("c")]
		public ChannelId ChannelId { get; set; }

		private ComputeTaskInfo()
		{
		}

		public ComputeTaskInfo(CbObjectAttachment TaskHash, ChannelId ChannelId)
		{
			this.TaskHash = TaskHash;
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
		public CbObjectAttachment Task { get; set; }

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
		public CbObjectAttachment? Result { get; set; }

		/// <inheritdoc/>
		[CbField("d")]
		public string? Detail { get; set; }

		/// <inheritdoc/>
		AgentId? IComputeTaskStatus.AgentId => AgentId.IsEmpty ? (AgentId?)null : new AgentId(AgentId.ToString());

		/// <inheritdoc/>
		public LeaseId? LeaseId
		{
			get => (LeaseIdBytes == null || LeaseIdBytes.Length == 0)? (LeaseId?)null : new LeaseId(LeaseIdBytes);
			set => LeaseIdBytes = (value != null) ? value.Value.Value.ToByteArray() : null;
		}

		private ComputeTaskStatus()
		{
		}

		public ComputeTaskStatus(CbObjectAttachment Task, ComputeTaskState State, AgentId? AgentId, LeaseId? LeaseId)
		{
			this.Task = Task;
			this.Time = DateTime.UtcNow;
			this.State = State;
			this.AgentId = (AgentId == null)? Utf8String.Empty : AgentId.Value.ToString();
			this.LeaseIdBytes = (LeaseId != null)? LeaseId.Value.Value.ToByteArray() : null;
		}
	}

	/// <summary>
	/// Dispatches remote actions. Does not implement any cross-pod communication to satisfy leases; only agents connected to this server instance will be stored.
	/// </summary>
	class ComputeService : TaskSourceBase<ComputeTaskMessage>, IComputeService, IDisposable
	{
		public override string Type => "Compute";

		public static NamespaceId DefaultNamespaceId { get; } = new NamespaceId("default");

		IObjectCollection ObjectCollection;
		IPoolCollection PoolCollection;
		ITaskScheduler<IoHash, ComputeTaskInfo> TaskScheduler;
		RedisMessageQueue<ComputeTaskStatus> MessageQueue;
		BackgroundTick ExpireTasksTicker;
		IMemoryCache RequirementsCache;
		LazyCachedValue<Task<List<IPool>>> CachedPools;
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Redis">Redis instance</param>
		/// <param name="ObjectCollection"></param>
		/// <param name="PoolCollection">Collection of pool documents</param>
		/// <param name="Logger">The logger instance</param>
		public ComputeService(IDatabase Redis, IObjectCollection ObjectCollection, IPoolCollection PoolCollection, ILogger<ComputeService> Logger)
		{
			this.ObjectCollection = ObjectCollection;
			this.PoolCollection = PoolCollection;
			this.TaskScheduler = new RedisTaskScheduler<IoHash, ComputeTaskInfo>(Redis, "compute/tasks/", Logger);
			this.MessageQueue = new RedisMessageQueue<ComputeTaskStatus>(Redis, "compute/messages/");
			this.ExpireTasksTicker = new BackgroundTick(ExpireTasksAsync, TimeSpan.FromMinutes(2.0), Logger);
			this.RequirementsCache = new MemoryCache(new MemoryCacheOptions());
			this.CachedPools = new LazyCachedValue<Task<List<IPool>>>(() => PoolCollection.GetAsync(), TimeSpan.FromSeconds(30.0));
			this.Logger = Logger;

			OnLeaseStartedProperties.Add(x => x.Task);
		}

		/// <summary>
		/// Expire tasks that are in inactive queues (ie. no machines can execute them)
		/// </summary>
		/// <param name="CancellationToken"></param>
		/// <returns></returns>
		async Task ExpireTasksAsync(CancellationToken CancellationToken)
		{
			List<IoHash> RequirementsHashes = await TaskScheduler.GetInactiveQueuesAsync();
			foreach (IoHash RequirementsHash in RequirementsHashes)
			{
				Logger.LogInformation("Inactive queue: {RequirementsHash}", RequirementsHash);
				for (; ; )
				{
					ComputeTaskInfo? ComputeTask = await TaskScheduler.DequeueAsync(RequirementsHash);
					if (ComputeTask == null)
					{
						break;
					}

					ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.TaskHash, ComputeTaskState.Complete, null, null);
					Status.Outcome = ComputeTaskOutcome.Expired;
					Logger.LogInformation("Compute task expired (queue: {RequirementsHash}, task: {TaskHash}, channel: {ChannelId})", RequirementsHash, ComputeTask.TaskHash, ComputeTask.ChannelId);
					await MessageQueue.PostAsync(ComputeTask.ChannelId.ToString(), Status);
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

		/// <inheritdoc/>
		public async Task AddTasksAsync(NamespaceId NamespaceId, CbObjectAttachment RequirementsHash, List<CbObjectAttachment> TaskHashes, ChannelId ChannelId)
		{
			List<Task> Tasks = new List<Task>();
			foreach (CbObjectAttachment TaskHash in TaskHashes)
			{
				ComputeTaskInfo TaskInfo = new ComputeTaskInfo(TaskHash, ChannelId);
				Logger.LogDebug("Adding task {TaskHash} from channel {ChannelId} to queue {RequirementsHash}", TaskInfo.TaskHash.Hash, ChannelId, RequirementsHash);
				Tasks.Add(TaskScheduler.EnqueueAsync(RequirementsHash, TaskInfo, false));
			}
			await Task.WhenAll(Tasks);
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
			(IoHash, ComputeTaskInfo)? Entry = await TaskScheduler.DequeueAsync(RequirementsHash => CheckRequirements(Agent, RequirementsHash), CancellationToken);
			if (Entry != null)
			{
				(IoHash RequirementsHash, ComputeTaskInfo TaskInfo) = Entry.Value;

				Requirements? Requirements = await GetCachedRequirementsAsync(RequirementsHash);
				if (Requirements != null)
				{
					ComputeTaskMessage ComputeTask = new ComputeTaskMessage();
					ComputeTask.ChannelId = TaskInfo.ChannelId.ToString();
					ComputeTask.NamespaceId = DefaultNamespaceId.ToString();
					ComputeTask.Requirements = new CbObjectAttachment(RequirementsHash);
					ComputeTask.Task = TaskInfo.TaskHash;

					string LeaseName = $"Remote action ({TaskInfo.TaskHash})";
					byte[] Payload = Any.Pack(ComputeTask).ToByteArray();

					AgentLease Lease = new AgentLease(LeaseId.GenerateNewId(), LeaseName, null, null, null, LeaseState.Pending, Requirements.Resources, Requirements.Exclusive, Payload);
					Logger.LogDebug("Created lease {LeaseId} for channel {ChannelId} task {TaskHash} req {RequirementsHash}", Lease.Id, ComputeTask.ChannelId, ComputeTask.Task.Hash, ComputeTask.Requirements.Hash);
					return Lease;
				}
			}
			return null;
		}

		/// <inheritdoc/>
		public override Task CancelLeaseAsync(IAgent Agent, LeaseId LeaseId, ComputeTaskMessage Message)
		{
			ComputeTaskInfo TaskInfo = new ComputeTaskInfo(Message.Task, new ChannelId(Message.ChannelId));
			return TaskScheduler.EnqueueAsync(Message.Requirements.Hash, TaskInfo, true);
		}

		/// <inheritdoc/>
		public async Task<List<IComputeTaskStatus>> GetTaskUpdatesAsync(ChannelId ChannelId)
		{
			List<ComputeTaskStatus> Messages = await MessageQueue.ReadMessagesAsync(ChannelId.ToString());
			return Messages.ConvertAll<IComputeTaskStatus>(x => x);
		}

		public async Task<List<IComputeTaskStatus>> WaitForTaskUpdatesAsync(ChannelId ChannelId, CancellationToken CancellationToken)
		{
			List<ComputeTaskStatus> Messages = await MessageQueue.WaitForMessagesAsync(ChannelId.ToString(), CancellationToken);
			return Messages.ConvertAll<IComputeTaskStatus>(x => x);
		}

		public override Task OnLeaseStartedAsync(IAgent Agent, LeaseId LeaseId, ComputeTaskMessage ComputeTask, ILogger Logger)
		{
			base.OnLeaseStartedAsync(Agent, LeaseId, ComputeTask, Logger);

			ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.Task, ComputeTaskState.Executing, Agent.Id, LeaseId);
			return MessageQueue.PostAsync(ComputeTask.ChannelId, Status);
		}

		public override Task OnLeaseFinishedAsync(IAgent Agent, LeaseId LeaseId, ComputeTaskMessage ComputeTask, LeaseOutcome Outcome, ReadOnlyMemory<byte> Output, ILogger Logger)
		{
			ComputeTaskResultMessage Message = ComputeTaskResultMessage.Parser.ParseFrom(Output.ToArray());

			ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.Task, ComputeTaskState.Complete, Agent.Id, LeaseId);
			if (Message.Result != null)
			{
				Status.Result = Message.Result;
			}
			else if (Message.Outcome != ComputeTaskOutcome.Success)
			{
				(Status.Outcome, Status.Detail) = (Message.Outcome, Message.Detail);
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

			Logger.LogInformation("Compute lease finished (lease: {LeaseId}, task: {TaskHash}, agent: {AgentId}, channel: {ChannelId}, result: {ResultHash}, outcome: {Outcome})", LeaseId, ComputeTask.Task.Hash, Agent.Id, ComputeTask.ChannelId, Status.Result?.Hash ?? IoHash.Zero, Status.Outcome);
			return MessageQueue.PostAsync(ComputeTask.ChannelId, Status);
		}

		/// <summary>
		/// Checks that an agent matches the necessary criteria to execute a task
		/// </summary>
		/// <param name="Agent"></param>
		/// <param name="RequirementsHash"></param>
		/// <returns></returns>
		async ValueTask<bool> CheckRequirements(IAgent Agent, IoHash RequirementsHash)
		{
			Requirements? Requirements = await GetCachedRequirementsAsync(RequirementsHash);
			if (Requirements == null)
			{
				return false;
			}
			return Agent.MeetsRequirements(Requirements);
		}

		/// <summary>
		/// Gets the requirements object from the CAS
		/// </summary>
		/// <param name="RequirementsHash"></param>
		/// <returns></returns>
		async ValueTask<Requirements?> GetCachedRequirementsAsync(IoHash RequirementsHash)
		{
			Requirements? Requirements;
			if (!RequirementsCache.TryGetValue(RequirementsHash, out Requirements))
			{
				Requirements = await ObjectCollection.GetAsync<Requirements>(DefaultNamespaceId, RequirementsHash);
				using (ICacheEntry Entry = RequirementsCache.CreateEntry(RequirementsHash))
				{
					if (Requirements == null)
					{
						Entry.SetAbsoluteExpiration(TimeSpan.FromSeconds(10.0));
					}
					else
					{
						Entry.SetSlidingExpiration(TimeSpan.FromMinutes(10.0));
					}
					Entry.SetValue(Requirements);
				}
			}
			return Requirements;
		}
	}

	/// <summary>
	/// Implementation of the gRPC compute service interface
	/// </summary>
	class ComputeRpcServer : ComputeRpc.ComputeRpcBase
	{
		IComputeService ComputeService;

		public ComputeRpcServer(IComputeService ComputeService)
		{
			this.ComputeService = ComputeService;
		}

		public override async Task<AddTasksRpcResponse> AddTasks(AddTasksRpcRequest RpcRequest, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(RpcRequest.NamespaceId);
			ChannelId ChannelId = new ChannelId(RpcRequest.ChannelId);
			await ComputeService.AddTasksAsync(NamespaceId, RpcRequest.RequirementsHash, RpcRequest.TaskHashes.Select(x => (CbObjectAttachment)x).ToList(), ChannelId);
			return new AddTasksRpcResponse();
		}

		public override async Task GetTaskUpdates(IAsyncStreamReader<GetTaskUpdatesRpcRequest> RequestStream, IServerStreamWriter<GetTaskUpdatesRpcResponse> ResponseStream, ServerCallContext Context)
		{
			Task<bool> MoveNextTask = RequestStream.MoveNext();
			while(await MoveNextTask)
			{
				GetTaskUpdatesRpcRequest Request = RequestStream.Current;

				ChannelId ChannelId = new ChannelId(Request.ChannelId);
				using (CancellationTokenSource CancellationSource = new CancellationTokenSource())
				{
					MoveNextTask = MoveNextAndCancel(RequestStream, CancellationSource);
					while (!CancellationSource.IsCancellationRequested)
					{
						List<IComputeTaskStatus> Updates = await ComputeService.WaitForTaskUpdatesAsync(ChannelId, CancellationSource.Token);
						foreach (IComputeTaskStatus Update in Updates)
						{
							GetTaskUpdatesRpcResponse Response = new GetTaskUpdatesRpcResponse();
							Response.Task = Update.Task;
							Response.Time = Timestamp.FromDateTime(Update.Time);
							Response.State = Update.State;
							Response.Result = Update.Result;
							await ResponseStream.WriteAsync(Response);
						}
					}
					await MoveNextTask;
				}
			}
		}

		static async Task<bool> MoveNextAndCancel(IAsyncStreamReader<GetTaskUpdatesRpcRequest> RequestStream, CancellationTokenSource CancellationSource)
		{
			bool Result = await RequestStream.MoveNext();
			CancellationSource.Cancel();
			return Result;
		}
	}
}
