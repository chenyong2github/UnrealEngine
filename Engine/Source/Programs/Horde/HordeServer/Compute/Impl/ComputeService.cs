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

namespace HordeServer.Compute.Impl
{
	using ChannelId = StringId<IComputeChannel>;
	using NamespaceId = StringId<INamespace>;

	/// <summary>
	/// Information about a particular task
	/// </summary>
	[RedisConverter(typeof(RedisCbConverter<>))]
	class ComputeTaskInfo
	{
		[CbField("h")]
		public IoHash TaskHash { get; set; }

		[CbField("c")]
		public ChannelId ChannelId { get; set; }

		private ComputeTaskInfo()
		{
		}

		public ComputeTaskInfo(IoHash TaskHash, ChannelId ChannelId)
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
		public IoHash TaskHash { get; set; }

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
		public IoHash? ResultHash { get; set; }

		/// <inheritdoc/>
		AgentId? IComputeTaskStatus.AgentId => AgentId.IsEmpty ? (AgentId?)null : new AgentId(AgentId.ToString());

		/// <inheritdoc/>
		public ObjectId? LeaseId
		{
			get => new ObjectId(LeaseIdBytes);
			set => LeaseIdBytes = value?.ToByteArray();
		}

		private ComputeTaskStatus()
		{
		}

		public ComputeTaskStatus(IoHash TaskHash, ComputeTaskState State)
		{
			this.TaskHash = TaskHash;
			this.Time = DateTime.UtcNow;
			this.State = State;
		}
	}

	/// <summary>
	/// Dispatches remote actions. Does not implement any cross-pod communication to satisfy leases; only agents connected to this server instance will be stored.
	/// </summary>
	class ComputeService : IComputeService, IDisposable
	{
		public MessageDescriptor Descriptor => ComputeTaskMessage.Descriptor;

		public NamespaceId NamespaceId { get; } = new NamespaceId("default");

		ITaskScheduler<ComputeTaskInfo> TaskScheduler;
		RedisMessageQueue<ComputeTaskStatus> MessageQueue;
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Redis">Redis instance</param>
		/// <param name="ObjectCollection"></param>
		/// <param name="Logger">The logger instance</param>
		public ComputeService(IDatabase Redis, IObjectCollection ObjectCollection, ILogger<ComputeService> Logger)
		{
			this.TaskScheduler = new RedisTaskScheduler<ComputeTaskInfo>(Redis, ObjectCollection, NamespaceId, "compute/tasks/");
			this.MessageQueue = new RedisMessageQueue<ComputeTaskStatus>(Redis, "compute/messages/");
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			MessageQueue.Dispose();
		}

		/// <inheritdoc/>
		public async Task AddTasksAsync(NamespaceId NamespaceId, IoHash RequirementsHash, List<IoHash> TaskHashes, ChannelId ChannelId)
		{
			List<Task> Tasks = new List<Task>();
			foreach (IoHash TaskHash in TaskHashes)
			{
				ComputeTaskInfo TaskInfo = new ComputeTaskInfo(TaskHash, ChannelId);
				Tasks.Add(TaskScheduler.EnqueueAsync(TaskInfo, RequirementsHash));
			}
			await Task.WhenAll(Tasks);
		}

		/// <inheritdoc/>
		public async Task<AgentLease?> TryAssignLeaseAsync(IAgent Agent, CancellationToken CancellationToken)
		{
			Console.WriteLine("TRY ASSIGN LEASE");
			TaskSchedulerEntry<ComputeTaskInfo>? Entry = await TaskScheduler.DequeueAsync(Agent, CancellationToken);
			if(Entry != null)
			{
				Console.WriteLine("GOT IT");
				ComputeTaskMessage ComputeTask = new ComputeTaskMessage();
				ComputeTask.ChannelId = Entry.Item.ChannelId.ToString();
				ComputeTask.NamespaceId = NamespaceId.ToString();
				ComputeTask.RequirementsHash = Entry.RequirementsHash;
				ComputeTask.TaskHash = Entry.Item.TaskHash;

				string LeaseName = $"Remote action ({Entry.Item.TaskHash})";
				byte[] Payload = Any.Pack(ComputeTask).ToByteArray();
				return new AgentLease(ObjectId.GenerateNewId(), LeaseName, null, null, null, LeaseState.Pending, Payload, new AgentRequirements(), null);
			}
			return null;
		}

		/// <inheritdoc/>
		public Task CancelLeaseAsync(IAgent Agent, ObjectId LeaseId, Any Payload)
		{
			ComputeTaskMessage Message = Payload.Unpack<ComputeTaskMessage>();
			ComputeTaskInfo TaskInfo = new ComputeTaskInfo(Message.TaskHash, new ChannelId(Message.ChannelId));
			return TaskScheduler.EnqueueAsync(TaskInfo, Message.RequirementsHash);
		}

		/// <inheritdoc/>
		public async Task<List<IComputeTaskStatus>> GetTaskUpdatesAsync(ChannelId ChannelId, CancellationToken CancellationToken)
		{
			List<ComputeTaskStatus> Messages = await MessageQueue.ReadAsync(ChannelId.ToString(), CancellationToken);
			return Messages.ConvertAll<IComputeTaskStatus>(x => x);
		}

		public Task OnLeaseStartedAsync(IAgent Agent, ObjectId LeaseId, Any Payload)
		{
			ComputeTaskMessage ComputeTask = Payload.Unpack<ComputeTaskMessage>();

			ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.TaskHash, ComputeTaskState.Executing);
			return MessageQueue.PostAsync(ComputeTask.ChannelId, Status);
		}

		public Task OnLeaseFinishedAsync(IAgent Agent, ObjectId LeaseId, Any Payload, LeaseOutcome Outcome, ReadOnlyMemory<byte> Output)
		{
			ComputeTaskMessage ComputeTask = Payload.Unpack<ComputeTaskMessage>();

			Logger.LogInformation("Task complete: {Hash}", (IoHash)ComputeTask.TaskHash);
			ComputeTaskResultMessage Result = ComputeTaskResultMessage.Parser.ParseFrom(Output.ToArray());

			ComputeTaskStatus Status = new ComputeTaskStatus(ComputeTask.TaskHash, ComputeTaskState.Complete);
			Status.LeaseId = LeaseId;
			if (Result.OutputHash != null)
			{
				Status.ResultHash = Result.OutputHash;
			}

			return MessageQueue.PostAsync(ComputeTask.ChannelId, Status);
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
			await ComputeService.AddTasksAsync(NamespaceId, RpcRequest.RequirementsHash, RpcRequest.TaskHashes.Select(x => (IoHash)x).ToList(), ChannelId);
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
						List<IComputeTaskStatus> Updates = await ComputeService.GetTaskUpdatesAsync(ChannelId, CancellationSource.Token);
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
