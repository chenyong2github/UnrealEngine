using Build.Bazel.Remote.Execution.V2;
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

namespace HordeServer.Tasks.Impl
{
	/// <summary>
	/// Interface for an action operation
	/// </summary>
	public interface IActionExecuteOperation
	{
		/// <summary>
		/// The unique id for this operation
		/// </summary>
		ObjectId Id { get; }

		/// <summary>
		/// Sets the result for this operation
		/// </summary>
		/// <param name="Result">The result for this operation. If null, the operation will be rescheduled to operate on a new worker</param>
		/// <returns></returns>
		bool TrySetResult(ActionResult? Result);

		/// <summary>
		/// Read status updates from the operation
		/// </summary>
		/// <returns>Sequence of operation updates</returns>
		IAsyncEnumerable<Operation> ReadStatusUpdatesAsync();
	}

	/// <summary>
	/// Dispatches remote actions. Does not implement any cross-pod communication to satisfy leases; only agents connected to this server instance will be stored.
	/// </summary>
	public class ActionTaskSource : ITaskSource
	{
		/// <summary>
		/// Number of buckets to create for storing operations. Operations are garbage collected from random buckets.
		/// </summary>
		const int NumBuckets = 1024;

		/// <summary>
		/// Number of buckets to sample when finding an operation to evict from the cache. Only operations older than <see cref="MinAgeForGC"/> will be considered for deletion.
		/// </summary>
		const int NumSamplesForGC = 4;

		/// <summary>
		/// Minimum age for operations to be garbage collected
		/// </summary>
		static readonly long MinAgeForGC = TimeSpan.FromMinutes(15.0).Ticks;

		/// <summary>
		/// Tracks an execution operation
		/// </summary>
		class ExecuteOperation : IActionExecuteOperation
		{
			/// <summary>
			/// The operation id
			/// </summary>
			public ObjectId Id { get; }

			/// <summary>
			/// The execution request
			/// </summary>
			public ExecuteRequest Request { get; }

			/// <summary>
			/// Timestamp indicating the last time the operation was accessed. Used to evict items from the list of tracked operations.
			/// </summary>
			public long Timestamp = Stopwatch.GetTimestamp();

			/// <summary>
			/// The executing task
			/// </summary>
			public Task? BackgroundTask;

			/// <summary>
			/// The current status of the operation
			/// </summary>
			public Operation? Status;

			/// <summary>
			/// Event which is signalled whenever the status changes
			/// </summary>
			TaskCompletionSource<bool> StateChangedEvent = new TaskCompletionSource<bool>();

			/// <summary>
			/// The result from executing this operation
			/// </summary>
			public TaskCompletionSource<ActionResult?> ResultTaskSource = new TaskCompletionSource<ActionResult?>();

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Request">The request to execute</param>
			public ExecuteOperation(ExecuteRequest Request)
			{
				this.Id = ObjectId.GenerateNewId();
				this.Request = Request;
			}

			/// <summary>
			/// Sets the operation result
			/// </summary>
			/// <param name="Result">The result of the operation</param>
			public bool TrySetResult(ActionResult? Result)
			{
				return ResultTaskSource.TrySetResult(Result);
			}

			/// <summary>
			/// Updates the current status
			/// </summary>
			/// <param name="Stage">The current stage of execution</param>
			/// <param name="Response">The final response from the operation</param>
			/// <returns>False if the operation is already complete</returns>
			public bool SetStatus(ExecutionStage.Types.Value Stage, Any? Response)
			{
				ExecuteOperationMetadata Metadata = new ExecuteOperationMetadata();
				Metadata.Stage = Stage;
				Metadata.ActionDigest = Request.ActionDigest;

				Operation Operation = new Operation();
				if (Response != null)
				{
					Operation.Response = Response;
					Operation.Done = true;
				}
				Operation.Metadata = Any.Pack(Metadata);

				return TrySetStatus(Operation);
			}

			/// <summary>
			/// Updates the current status
			/// </summary>
			/// <param name="NewStatus">The operation status</param>
			/// <returns>False if the operation is already complete</returns>
			public bool TrySetStatus(Operation NewStatus)
			{
				for (; ; )
				{
					TaskCompletionSource<bool> PrevStateChangedEvent = StateChangedEvent;

					Operation? CurrentStatus = Status;
					if (CurrentStatus != null && CurrentStatus.Done)
					{
						return false;
					}
					if (Interlocked.CompareExchange(ref Status, NewStatus, CurrentStatus) == CurrentStatus)
					{
						TaskCompletionSource<bool> NextStateChangedEvent = new TaskCompletionSource<bool>();
						while (Interlocked.CompareExchange(ref StateChangedEvent, NextStateChangedEvent, PrevStateChangedEvent) != PrevStateChangedEvent)
						{
							PrevStateChangedEvent = StateChangedEvent;
						}
						PrevStateChangedEvent.TrySetResult(true);
						return true;
					}
				}
			}

			/// <summary>
			/// Read status updates from the operation
			/// </summary>
			/// <returns>Sequence of operation updates</returns>
			public async IAsyncEnumerable<Operation> ReadStatusUpdatesAsync()
			{
				Operation? LastStatus = null;
				for (; ; )
				{
					TaskCompletionSource<bool> StateChangedEventCopy = StateChangedEvent;

					Operation? NextStatus = Status;
					if (NextStatus != LastStatus)
					{
						yield return NextStatus!;

						if (NextStatus!.Done)
						{
							break;
						}
					}
					LastStatus = NextStatus;

					await StateChangedEventCopy.Task;

					Interlocked.CompareExchange(ref StateChangedEvent, new TaskCompletionSource<bool>(), StateChangedEventCopy);
				}
			}
		}

		/// <summary>
		/// Tracks an agent waiting for work
		/// </summary>
		class Subscription : ITaskListener
		{
			/// <summary>
			/// The owning instance
			/// </summary>
			ActionTaskSource Outer;

			/// <summary>
			/// Agent waiting for work
			/// </summary>
			public IAgent Agent { get; }

			/// <summary>
			/// The operation assigned to this subscription. May be set to null if the subscription is cancelled.
			/// </summary>
			public TaskCompletionSource<NewLeaseInfo?> LeaseTaskSource = new TaskCompletionSource<NewLeaseInfo?>();

			/// <inheritdoc/>
			public Task<NewLeaseInfo?> LeaseTask => LeaseTaskSource.Task;

			/// <inheritdoc/>
			public bool Accepted { get; set; }

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Outer">The task source</param>
			/// <param name="Agent">The agent instance</param>
			public Subscription(ActionTaskSource Outer, IAgent Agent)
			{
				this.Outer = Outer;
				this.Agent = Agent;

				lock (Outer.Subscriptions)
				{
					Outer.Subscriptions.Add(this);
				}
			}

			/// <summary>
			/// Attempts to assign a lease to this agent
			/// </summary>
			/// <param name="Lease">The lease to assign</param>
			/// <param name="OnConnectionLost"></param>
			/// <returns>True if the lease was set, false otherwise</returns>
			public bool TrySetLease(AgentLease Lease, System.Action OnConnectionLost)
			{
				return LeaseTaskSource.TrySetResult(new NewLeaseInfo(Lease, OnConnectionLost));
			}

			/// <summary>
			/// Cancel of this subscription
			/// </summary>
			/// <returns></returns>
			public async ValueTask DisposeAsync()
			{
				// Remove from the list of active subscriptions
				lock (Outer.Subscriptions)
				{
					Outer.Subscriptions.Remove(this);
				}

				// If an operation has been assigned to this subscription and it wasn't accepted, attempt to reassign it
				if (!LeaseTaskSource.TrySetResult(null) && !Accepted)
				{
					NewLeaseInfo? NewLeaseInfo = await LeaseTaskSource.Task;
					if (NewLeaseInfo != null)
					{
						await Outer.CancelLeaseAsync(NewLeaseInfo.Lease);
					}
				}
			}
		}

		ActionCacheService ActionCacheService;
		ILogFileService LogFileService;
		HashSet<Subscription> Subscriptions = new HashSet<Subscription>();
		Dictionary<ObjectId, ExecuteOperation>[] ActiveOperationBuckets = new Dictionary<ObjectId, ExecuteOperation>[NumBuckets];
		Random Random = new Random();
		ILogger Logger;

		/// <inheritdoc/>
		public MessageDescriptor Descriptor => ActionTask.Descriptor;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ActionCacheService"></param>
		/// <param name="LogFileService">The log file service instance</param>
		/// <param name="Logger">The logger instance</param>
		public ActionTaskSource(ActionCacheService ActionCacheService, ILogFileService LogFileService, ILogger<ActionTaskSource> Logger)
		{
			this.ActionCacheService = ActionCacheService;
			this.LogFileService = LogFileService;
			this.Logger = Logger;

			for (int Idx = 0; Idx < NumBuckets; Idx++)
			{
				ActiveOperationBuckets[Idx] = new Dictionary<ObjectId, ExecuteOperation>();
			}
		}

		Task CancelLeaseAsync(AgentLease Lease)
		{
			ExecuteOperation? Operation;
			if (TryGetOperation(Lease.Id, out Operation))
			{
				Operation.TrySetResult(null);
			}
			return Task.CompletedTask;
		}

		static int GetBucketIdx(ObjectId Id)
		{
			return Id.GetHashCode() & (NumBuckets - 1);
		}

		Dictionary<ObjectId, ExecuteOperation> GetBucket(ObjectId Id)
		{
			return ActiveOperationBuckets[GetBucketIdx(Id)];
		}

		/// <summary>
		/// Attempts to assign an action for execution
		/// </summary>
		/// <param name="Request">Execution request</param>
		/// <returns>The operation created to execute this action</returns>
		public IActionExecuteOperation Execute(ExecuteRequest Request)
		{
			ExecuteOperation ExecuteOperation = new ExecuteOperation(Request);
			ExecuteOperation.BackgroundTask = Task.Run(() => RunAsync(ExecuteOperation));

			Dictionary<ObjectId, ExecuteOperation> Bucket = GetBucket(ExecuteOperation.Id);
			lock (Bucket)
			{
				Bucket[ExecuteOperation.Id] = ExecuteOperation;
			}

			TrimOnce();

			return ExecuteOperation;
		}

		/// <summary>
		/// Run the task as a background operation
		/// </summary>
		/// <returns></returns>
		async Task RunAsync(ExecuteOperation Operation)
		{
			ExecuteResponse Response = await RunInternalAsync(Operation);
			Operation.SetStatus(ExecutionStage.Types.Value.Completed, Any.Pack(Response));
		}

		/// <summary>
		/// Run the task as a background operation
		/// </summary>
		/// <returns></returns>
		async Task<ExecuteResponse> RunInternalAsync(ExecuteOperation Operation)
		{
			ExecuteRequest Request = Operation.Request;
			if (!Request.SkipCacheLookup)
			{
				Operation.SetStatus(ExecutionStage.Types.Value.CacheCheck, null);

				ActionResult? Result = await ActionCacheService.TryGetResult(Request.ActionDigest);
				if (Result != null)
				{
					ExecuteResponse CachedResponse = new ExecuteResponse();
					CachedResponse.Result = Result;
					CachedResponse.CachedResult = true;
					CachedResponse.Status = new Status(StatusCode.OK, String.Empty);
					return CachedResponse;
				}
			}

			for(; ;)
			{
				Operation.SetStatus(ExecutionStage.Types.Value.Queued, null);

				// Get a new subscription
				Subscription? Subscription = await WaitForSubscriberAsync(TimeSpan.FromSeconds(20.0));
				if (Subscription == null)
				{
					ExecuteResponse FailedResponse = new ExecuteResponse();
					FailedResponse.Status = new Status(StatusCode.ResourceExhausted, "Unable to find agent to execute request");
					return FailedResponse;
				}

				// Create a lease for it
				string LeaseName = $"Remote action ({Request.ActionDigest.Hash.Substring(0, 16)}...)";

				ILogFile LogFile = await LogFileService.CreateLogFileAsync(ObjectId.Empty, Subscription.Agent.SessionId, LogType.Json);

				ActionTask ActionTask = new ActionTask();
				ActionTask.Digest = Request.ActionDigest;
				ActionTask.LogId = LogFile.Id.ToString();

				byte[] Payload = Any.Pack(ActionTask).ToByteArray();
				AgentLease Lease = new AgentLease(Operation.Id, LeaseName, null, null, LogFile.Id, LeaseState.Pending, Payload, new AgentRequirements(), null);

				// Try to set the lease on the subscription. If it fails, the subscriber has already been allocated
				Operation.ResultTaskSource = new TaskCompletionSource<ActionResult?>();
				if (Subscription.TrySetLease(Lease, () => Operation.ResultTaskSource.TrySetResult(null)))
				{
					Operation.SetStatus(ExecutionStage.Types.Value.Executing, null);

					ActionResult? Result = await Operation.ResultTaskSource.Task;
					if (Result != null)
					{
						ExecuteResponse SuccessResponse = new ExecuteResponse();
						SuccessResponse.Status = new Status(StatusCode.OK, String.Empty);
						SuccessResponse.Result = Result;
						return SuccessResponse;
					}
				}
			}
		}

		/// <summary>
		/// Waits until there is an available agent for executing work
		/// </summary>
		/// <param name="TimeLimit"></param>
		/// <returns></returns>
		async Task<Subscription?> WaitForSubscriberAsync(TimeSpan TimeLimit)
		{
			Stopwatch Timer = Stopwatch.StartNew();
			for (; ; )
			{
				// Try to allocate a subscription
				lock (Subscriptions)
				{
					Subscription? Subscription = Subscriptions.FirstOrDefault();
					if (Subscription != null)
					{
						Subscriptions.Remove(Subscription);
						return Subscription;
					}
				}

				// Wait until the next poll time
				TimeSpan Delay = TimeLimit - Timer.Elapsed;
				if (Delay < TimeSpan.Zero)
				{
					return null;
				}
				TimeSpan MaxDelay = TimeSpan.FromMilliseconds(200);
				if (Delay > MaxDelay)
				{
					Delay = MaxDelay;
				}
				await Task.Delay(Delay);
			}
		}

		/// <summary>
		/// Attempts to get an existing operation by id
		/// </summary>
		/// <param name="OperationId"></param>
		/// <param name="Operation"></param>
		/// <returns></returns>
		bool TryGetOperation(ObjectId OperationId, [NotNullWhen(true)] out ExecuteOperation? Operation)
		{
			Dictionary<ObjectId, ExecuteOperation> Bucket = GetBucket(OperationId);
			lock (Bucket)
			{
				return Bucket.TryGetValue(OperationId, out Operation);
			}
		}

		/// <summary>
		/// Attempts to get an existing operation by id
		/// </summary>
		/// <param name="OperationId"></param>
		/// <param name="Operation"></param>
		/// <returns></returns>
		public bool TryGetOperation(ObjectId OperationId, [NotNullWhen(true)] out IActionExecuteOperation? Operation)
		{
			ExecuteOperation? ExecuteOperation;
			if (TryGetOperation(OperationId, out ExecuteOperation))
			{
				Operation = ExecuteOperation;
				return true;
			}
			else
			{
				Operation = null;
				return false;
			}
		}

		void TrimOnce()
		{
			ObjectId? LeaseId = null;
			ExecuteOperation? Operation = null;
			Dictionary<ObjectId, ExecuteOperation>? Bucket = null;

			// Sample several buckets and find the oldest operation
			long Timestamp = Stopwatch.GetTimestamp() - MinAgeForGC;
			for (int AttemptIdx = 0; AttemptIdx < NumSamplesForGC; AttemptIdx++)
			{
				Dictionary<ObjectId, ExecuteOperation> NextBucket = ActiveOperationBuckets[Random.Next() & (NumBuckets - 1)];
				lock(NextBucket)
				{
					foreach (KeyValuePair<ObjectId, ExecuteOperation> NextItem in NextBucket)
					{
						if (NextItem.Value.Timestamp < Timestamp && (NextItem.Value.BackgroundTask?.IsCompleted ?? true))
						{
							Bucket = NextBucket;
							LeaseId = NextItem.Key;
							Operation = NextItem.Value;
							Timestamp = NextItem.Value.Timestamp;
							break;
						}
					}
				}
			}

			// If we found one, 
			if (LeaseId != null)
			{
				lock (Bucket!)
				{
					Operation!.TrySetResult(null);
					Bucket.Remove(LeaseId.Value);
				}
			}
		}

		/// <inheritdoc/>
		public Task<ITaskListener?> SubscribeAsync(IAgent Agent)
		{
			return Task.FromResult<ITaskListener?>(new Subscription(this, Agent));
		}

		/// <inheritdoc/>
		public Task AbortTaskAsync(IAgent Agent, ObjectId LeaseId, Any Payload)
		{
			return Task.CompletedTask;
		}
	}
}
