// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Google.LongRunning;
using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Rpc;
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
	using NamespaceId = StringId<INamespace>;
	using Condition = HordeServer.Utilities.Condition;

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
		public CbObjectAttachment TaskHash { get; set; }

		/// <inheritdoc/>
		[CbField("t")]
		public DateTime Time { get; set; }

		/// <inheritdoc/>
		[CbField("s")]
		public ComputeTaskState State { get; set; }

		/// <inheritdoc cref="IComputeTaskStatus.AgentId"/>
		[CbField("a")]
		public Utf8String AgentId { get; set; }

		/// <inheritdoc cref="IComputeTaskStatus.LeaseId"/>
		[CbField("l")]
		byte[]? LeaseIdBytes { get; set; }

		/// <inheritdoc/>
		[CbField("r")]
		public CbObjectAttachment? ResultHash { get; set; }

		/// <inheritdoc/>
		AgentId? IComputeTaskStatus.AgentId => AgentId.IsEmpty ? (AgentId?)null : new AgentId(AgentId.ToString());

		/// <inheritdoc/>
		public ObjectId? LeaseId
		{
			get => (LeaseIdBytes == null)? (ObjectId?)null : new ObjectId(LeaseIdBytes);
			set => LeaseIdBytes = value?.ToByteArray();
		}

		private ComputeTaskStatus()
		{
		}

		public ComputeTaskStatus(CbObjectAttachment TaskHash, ComputeTaskState State, AgentId? AgentId, ObjectId? LeaseId)
		{
			this.TaskHash = TaskHash;
			this.Time = DateTime.UtcNow;
			this.State = State;
			this.AgentId = (AgentId == null)? Utf8String.Empty : AgentId.Value.ToString();
			this.LeaseIdBytes = LeaseId?.ToByteArray();
		}
	}

	/// <summary>
	/// Dispatches remote actions. Does not implement any cross-pod communication to satisfy leases; only agents connected to this server instance will be stored.
	/// </summary>
	class ComputeService : IComputeService, IDisposable
	{
		class QueueInfo
		{
			public Requirements? Requirements;
			public Condition? Condition;
		}

		public MessageDescriptor Descriptor => ComputeTaskMessage.Descriptor;

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
				for (; ; )
				{
					ComputeTaskInfo? ComputeTask = await TaskScheduler.DequeueAsync(RequirementsHash);
					if (ComputeTask == null)
					{
						break;
					}

					ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.TaskHash, ComputeTaskState.Complete, null, null);
					Logger.LogInformation("Compute task cancelled (task: {TaskHash}, channel: {ChannelId})", ComputeTask.TaskHash, ComputeTask.ChannelId);
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
				Logger.LogDebug("Adding task {TaskHash} to queue {RequirementsHash}", TaskInfo.TaskHash.Hash, RequirementsHash);
				Tasks.Add(TaskScheduler.EnqueueAsync(RequirementsHash, TaskInfo));
			}
			await Task.WhenAll(Tasks);
		}

		/// <inheritdoc/>
		public async Task<AgentLease?> TryAssignLeaseAsync(IAgent Agent, CancellationToken CancellationToken)
		{
			// If the agent is disabled, just block until we cancel
			if (!Agent.Enabled)
			{
				using CancellationTask Task = new CancellationTask(CancellationToken);
				await Task.Task;
				return null;
			}

			// Get all the current agent properties
			Lazy<Task<List<string>>> Properties = new Lazy<Task<List<string>>>(() => GetAgentProperties(Agent));

			// Find a task to execute
			(IoHash, ComputeTaskInfo)? Entry = await TaskScheduler.DequeueAsync(RequirementsHash => CheckRequirements(RequirementsHash, Properties), CancellationToken);
			if (Entry != null)
			{
				(IoHash RequirementsHash, ComputeTaskInfo TaskInfo) = Entry.Value;

				ComputeTaskMessage ComputeTask = new ComputeTaskMessage();
				ComputeTask.ChannelId = TaskInfo.ChannelId.ToString();
				ComputeTask.NamespaceId = DefaultNamespaceId.ToString();
				ComputeTask.RequirementsHash = new CbObjectAttachment(RequirementsHash);
				ComputeTask.TaskHash = TaskInfo.TaskHash;

				string LeaseName = $"Remote action ({TaskInfo.TaskHash})";
				byte[] Payload = Any.Pack(ComputeTask).ToByteArray();

				AgentLease Lease = new AgentLease(ObjectId.GenerateNewId(), LeaseName, null, null, null, LeaseState.Pending, Payload, new AgentRequirements(), null);
				Logger.LogDebug("Created lease {LeaseId} for channel {ChannelId} task {TaskHash} req {RequirementsHash}", Lease.Id, ComputeTask.ChannelId, (CbObjectAttachment)ComputeTask.TaskHash, (CbObjectAttachment)ComputeTask.RequirementsHash);
				return Lease;
			}
			return null;
		}

		/// <inheritdoc/>
		public Task CancelLeaseAsync(IAgent Agent, ObjectId LeaseId, Any Payload)
		{
			ComputeTaskMessage Message = Payload.Unpack<ComputeTaskMessage>();
			ComputeTaskInfo TaskInfo = new ComputeTaskInfo(Message.TaskHash, new ChannelId(Message.ChannelId));
			return TaskScheduler.EnqueueAsync((CbObjectAttachment)Message.RequirementsHash, TaskInfo);
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

		public Task OnLeaseStartedAsync(IAgent Agent, ObjectId LeaseId, Any Payload)
		{
			ComputeTaskMessage ComputeTask = Payload.Unpack<ComputeTaskMessage>();
			Logger.LogInformation("Compute lease started (lease: {LeaseId}, task: {TaskHash}, agent: {AgentId}, channel: {ChannelId})", LeaseId, (CbObjectAttachment)ComputeTask.TaskHash, Agent.Id, ComputeTask.ChannelId);

			ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.TaskHash, ComputeTaskState.Executing, Agent.Id, LeaseId);
			return MessageQueue.PostAsync(ComputeTask.ChannelId, Status);
		}

		public Task OnLeaseFinishedAsync(IAgent Agent, ObjectId LeaseId, Any Payload, LeaseOutcome Outcome, ReadOnlyMemory<byte> Output)
		{
			ComputeTaskMessage ComputeTask = Payload.Unpack<ComputeTaskMessage>();

			ComputeTaskResultMessage Result = ComputeTaskResultMessage.Parser.ParseFrom(Output.ToArray());

			ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.TaskHash, ComputeTaskState.Complete, Agent.Id, LeaseId);
			if (Result.OutputHash != null)
			{
				Status.ResultHash = Result.OutputHash;
			}

			Logger.LogInformation("Compute lease finished (lease: {LeaseId}, task: {TaskHash}, agent: {AgentId}, channel: {ChannelId}, result: {ResultHash})", LeaseId, (CbObjectAttachment)ComputeTask.TaskHash, Agent.Id, ComputeTask.ChannelId, Status.ResultHash?.Hash ?? IoHash.Zero);
			return MessageQueue.PostAsync(ComputeTask.ChannelId, Status);
		}

		/// <summary>
		/// Checks that an agent matches the necessary criteria to execute a task
		/// </summary>
		/// <param name="RequirementsHash"></param>
		/// <param name="LazyProperties">Properties for this agentthat the agent is in</param>
		/// <returns></returns>
		async ValueTask<bool> CheckRequirements(IoHash RequirementsHash, Lazy<Task<List<string>>> LazyProperties)
		{
			QueueInfo QueueInfo = await GetQueueInfoAsync(RequirementsHash);
			if (QueueInfo.Requirements == null)
			{
				return false;
			}

			if (!QueueInfo.Requirements.Condition.IsEmpty)
			{
				if (QueueInfo.Condition == null)
				{
					return false;
				}

				List<string> Properties = await LazyProperties.Value;
				if (!QueueInfo.Condition.Evaluate(x => FindAgentProperty(x, Properties)))
				{
					return false;
				}
			}

			return true;
		}

		/// <summary>
		/// Finds property values from a sorted list of Name=Value pairs
		/// </summary>
		/// <param name="Name">Name of the property to find</param>
		/// <param name="Properties">The full list of properties</param>
		/// <returns>Property values</returns>
		static IEnumerable<string> FindAgentProperty(string Name, List<string> Properties)
		{
			int Index = Properties.BinarySearch(Name, StringComparer.OrdinalIgnoreCase);
			if (Index < 0)
			{
				Index = ~Index;
				while (Index < Properties.Count)
				{
					string Property = Properties[Index];
					if (Property.Length <= Name.Length || !Property.StartsWith(Name, StringComparison.OrdinalIgnoreCase) || Property[Name.Length] != '=')
					{
						break;
					}
					yield return Property.Substring(Name.Length + 1);
				}
			}
		}

		/// <summary>
		/// Gets a named property for an agent
		/// </summary>
		/// <param name="Agent">The agent instance</param>
		/// <returns>The property value, or an empty string if it does not eixst</returns>
		async Task<List<string>> GetAgentProperties(IAgent Agent)
		{
			List<string> Properties = new List<string>();
			Properties.Add($"Name={Agent.Id}");

			foreach (IPool Pool in Agent.GetPools(await CachedPools.GetCached()))
			{
				Properties.Add($"Pool={Pool.Id}");
			}

			HashSet<string>? DeviceProperties = Agent.Capabilities.PrimaryDevice.Properties;
			if (DeviceProperties != null)
			{
				Properties.AddRange(DeviceProperties);
			}

			Properties.Sort(StringComparer.OrdinalIgnoreCase);
			return Properties;
		}

		/// <summary>
		/// Gets the requirements object from the CAS
		/// </summary>
		/// <param name="RequirementsHash"></param>
		/// <returns></returns>
		async ValueTask<QueueInfo> GetQueueInfoAsync(IoHash RequirementsHash)
		{
			QueueInfo? Info;
			if (!RequirementsCache.TryGetValue(RequirementsHash, out Info))
			{
				Info = new QueueInfo();
				Info.Requirements = await ObjectCollection.GetAsync<Requirements>(DefaultNamespaceId, RequirementsHash);

				if (Info.Requirements != null && !Info.Requirements.Condition.IsEmpty)
				{
					try
					{
						Info.Condition = Condition.Parse(Info.Requirements.Condition.ToString());
					}
					catch (Exception Ex)
					{
						Logger.LogError(Ex, "Unable to parse condition '{Condition}': {Message}", Info.Requirements.Condition.ToString(), Ex.Message);
					}
				}

				using (ICacheEntry Entry = RequirementsCache.CreateEntry(RequirementsHash))
				{
					if (Info.Requirements == null)
					{
						Entry.SetAbsoluteExpiration(TimeSpan.FromSeconds(10.0));
					}
					else
					{
						Entry.SetSlidingExpiration(TimeSpan.FromMinutes(10.0));
					}
					Entry.SetValue(Info);
				}
			}
			return Info!;
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
							Response.TaskHash = Update.TaskHash;
							Response.Time = Timestamp.FromDateTime(Update.Time);
							Response.State = Update.State;
							Response.ResultHash = Update.ResultHash;
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
