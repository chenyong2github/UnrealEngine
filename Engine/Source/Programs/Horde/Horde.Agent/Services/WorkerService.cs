// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon.EC2;
using Amazon.EC2.Model;
using Amazon.Util;
using EpicGames.Core;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using Grpc.Net.Client;
using HordeAgent.Execution;
using HordeAgent.Execution.Interfaces;
using HordeAgent.Parser;
using HordeAgent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using HordeCommon.Rpc.Messages;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using OpenTracing;
using OpenTracing.Util;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Management;
using System.Net.Http.Headers;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Xml;
using Status = Grpc.Core.Status;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Compute;
using System.Net;

namespace HordeAgent.Services
{
	/// <summary>
	/// Implements the message handling loop for an agent. Runs asynchronously until disposed.
	/// </summary>
	class WorkerService : BackgroundService, IDisposable
	{
		/// <summary>
		/// Stores information about an active session
		/// </summary>
		internal class LeaseInfo
		{
			/// <summary>
			/// The worker lease state
			/// </summary>
			public Lease Lease;

			/// <summary>
			/// The task being executed for this lease
			/// </summary>
			public Task? Task;

			/// <summary>
			/// Source for cancellation tokens for this session.
			/// </summary>
			public CancellationTokenSource CancellationTokenSource;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Lease">The worker lease state</param>
			public LeaseInfo(Lease Lease)
			{
				this.Lease = Lease;
				this.CancellationTokenSource = new CancellationTokenSource();
			}
		}

		/// <summary>
		/// Result from executing a lease
		/// </summary>
		internal class LeaseResult
		{
			/// <summary>
			/// Outcome of the lease (whether it completed/failed due to an internal error, etc...)
			/// </summary>
			public LeaseOutcome Outcome { get; }

			/// <summary>
			/// Output from executing the task
			/// </summary>
			public byte[]? Output { get; }

			/// <summary>
			/// Static instance of a cancelled result
			/// </summary>
			public static LeaseResult Cancelled { get; } = new LeaseResult(LeaseOutcome.Cancelled);

			/// <summary>
			/// Static instance of a failed result
			/// </summary>
			public static LeaseResult Failed { get; } = new LeaseResult(LeaseOutcome.Failed);

			/// <summary>
			/// Static instance of a succesful result without a payload
			/// </summary>
			public static LeaseResult Success { get; } = new LeaseResult(LeaseOutcome.Success);

			/// <summary>
			/// Constructor for 
			/// </summary>
			/// <param name="Outcome"></param>
			private LeaseResult(LeaseOutcome Outcome)
			{
				this.Outcome = Outcome;
			}

			/// <summary>
			/// Constructor for successful results
			/// </summary>
			/// <param name="Output"></param>
			public LeaseResult(byte[]? Output)
			{
				this.Outcome = LeaseOutcome.Success;
				this.Output = Output;
			}
		}

		/// <summary>
		/// List of processes that should be terminated before running a job
		/// </summary>
		private HashSet<string> ProcessNamesToTerminate = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Object used for controlling access to the access tokens and active sessions list
		/// </summary>
		object LockObject = new object();

		/// <summary>
		/// Sink for log messages
		/// </summary>
		ILogger<WorkerService> Logger;

		/// <summary>
		/// The current settings instance
		/// </summary>
		AgentSettings Settings;

		/// <summary>
		/// Settings for the current server
		/// </summary>
		ServerProfile ServerProfile;

		/// <summary>
		/// The grpc service instance
		/// </summary>
		GrpcService GrpcService;

		/// <summary>
		/// The working directory
		/// </summary>
		DirectoryReference WorkingDir;

		/// <summary>
		/// The list of active leases.
		/// </summary>
		List<LeaseInfo> ActiveLeases = new List<LeaseInfo>();

		/// <summary>
		/// Whether the agent is currently in an unhealthy state
		/// </summary>
		bool Unhealthy = false;

		/// <summary>
		/// Whether the 
		/// </summary>
		bool RequestShutdown = false;

		/// <summary>
		/// Whether to restart after shutting down
		/// </summary>
		bool RestartAfterShutdown = false;

		/// <summary>
		/// Time the the service started
		/// </summary>
		static readonly DateTimeOffset StartTime = DateTimeOffset.Now;

		/// <summary>
		/// Time at which the computer started
		/// </summary>
		static readonly DateTimeOffset BootTime = DateTimeOffset.Now - TimeSpan.FromTicks(Environment.TickCount64 * TimeSpan.TicksPerMillisecond);

		/// <summary>
		/// Task completion source used to trigger the background thread to update the leases. Must take a lock on LockObject before 
		/// </summary>
		AsyncEvent UpdateLeasesEvent = new AsyncEvent();

		/// <summary>
		/// Number of times UpdateSession has failed
		/// </summary>
		int UpdateSessionFailures;

		/// <summary>
		/// Function for creating a new executor. Primarily done this way to aid testing. 
		/// </summary>
		private readonly Func<IRpcConnection, ExecuteJobTask, BeginBatchResponse, IExecutor> CreateExecutor;

		/// <summary>
		/// How often to poll the server checking if a step has been aborted
		/// Exposed as internal to ease testing. 
		/// </summary>
		internal TimeSpan StepAbortPollInterval = TimeSpan.FromSeconds(5);

		/// <summary>
		/// Constructor. Registers with the server and starts accepting connections.
		/// </summary>
		/// <param name="Logger">Log sink</param>
		/// <param name="OptionsMonitor">The current settings</param>
		/// <param name="GrpcService">Instance of the Grpc service</param>
		/// <param name="CreateExecutor"></param>
		public WorkerService(ILogger<WorkerService> Logger, IOptionsMonitor<AgentSettings> OptionsMonitor, GrpcService GrpcService, Func<IRpcConnection, ExecuteJobTask, BeginBatchResponse, IExecutor>? CreateExecutor = null)
		{
			this.Logger = Logger;
			this.Settings = OptionsMonitor.CurrentValue;
			this.ServerProfile = Settings.GetCurrentServerProfile();
			this.GrpcService = GrpcService;

			if (Settings.WorkingDir == null)
			{
				throw new Exception("WorkingDir is not set. Unable to run service.");
			}

			DirectoryReference BaseDir = new FileReference(Assembly.GetExecutingAssembly().Location).Directory;

			WorkingDir = DirectoryReference.Combine(BaseDir, Settings.WorkingDir);
			Logger.LogInformation("Using working directory {WorkingDir}", WorkingDir);
			DirectoryReference.CreateDirectory(WorkingDir);

			ProcessNamesToTerminate.UnionWith(Settings.ProcessNamesToTerminate);

			if (CreateExecutor == null)
			{
				this.CreateExecutor = Settings.Executor switch
				{
					ExecutorType.Test => (RpcClient, ExecuteTask, Batch) =>
						new TestExecutor(RpcClient, ExecuteTask.JobId, ExecuteTask.BatchId, Batch.AgentType),
					ExecutorType.Local => (RpcClient, ExecuteTask, Batch) =>
						new LocalExecutor(RpcClient, ExecuteTask.JobId, ExecuteTask.BatchId, Batch.AgentType, Settings.LocalExecutor),
					ExecutorType.Perforce => (RpcClient, ExecuteTask, Batch) =>
						new PerforceExecutor(RpcClient, ExecuteTask.JobId, ExecuteTask.BatchId, Batch.AgentType, ExecuteTask.AutoSdkWorkspace, ExecuteTask.Workspace, WorkingDir),
					_ => throw new InvalidDataException($"Unknown executor type '{Settings.Executor}'")
				};
			}
			else
			{
				this.CreateExecutor = CreateExecutor;
			}
		}

		/// <summary>
		/// Executes the ServerTaskAsync method and swallows the exception for the task being cancelled. This allows waiting for it to terminate.
		/// </summary>
		/// <param name="StoppingToken">Indicates that the service is trying to stop</param>
		protected override async Task ExecuteAsync(CancellationToken StoppingToken)
		{
			try
			{
				await ExecuteInnerAsync(StoppingToken);
			}
			catch (Exception Ex)
			{
				Logger.LogCritical(Ex, "Unhandled exception");
			}
		}

		/// <summary>
		/// Background task to cycle access tokens and update the state of the agent with the server.
		/// </summary>
		/// <param name="StoppingToken">Indicates that the service is trying to stop</param>
		async Task ExecuteInnerAsync(CancellationToken StoppingToken)
		{
			// Print the server info
			Logger.LogInformation("Server: {Server}", ServerProfile.Url);
			Logger.LogInformation("Arguments: {Arguments}", Environment.CommandLine);

			// Show the current client id
			string Version = Program.Version;
			Logger.LogInformation("Version: {Version}", Version);

			// Keep trying to start an agent session with the server
			while (!StoppingToken.IsCancellationRequested)
			{
				Stopwatch SessionTime = Stopwatch.StartNew();
				try
				{
					await HandleSessionAsync(StoppingToken);
				}
				catch (Exception Ex)
				{
					if (StoppingToken.IsCancellationRequested && IsCancellationException(Ex))
					{
						break;
					}
					else
					{
						Logger.LogError(Ex, "Exception while executing session. Restarting.");
					}
				}

				if (RequestShutdown)
				{
					Logger.LogInformation("Initiating shutdown (restart={Restart})", RestartAfterShutdown);
					if (Shutdown.InitiateShutdown(RestartAfterShutdown, Logger))
					{
						for (int Idx = 10; Idx > 0; Idx--)
						{
							Logger.LogInformation("Waiting for shutdown ({Count})", Idx);
							await Task.Delay(TimeSpan.FromSeconds(60.0));
						}
						Logger.LogInformation("Shutdown aborted.");
					}
					RequestShutdown = RestartAfterShutdown = false;
				}
				else if (SessionTime.Elapsed < TimeSpan.FromSeconds(2.0))
				{
					Logger.LogInformation("Waiting 5 seconds before restarting session...");
					await Task.Delay(TimeSpan.FromSeconds(5.0));
				}
			}
		}

		async Task WaitForMutexAsync(Mutex Mutex, CancellationToken StoppingToken)
		{
			try
			{
				if (!Mutex.WaitOne(0))
				{
					Logger.LogError("Another instance of HordeAgent is already running. Waiting until it terminates.");
					while (!Mutex.WaitOne(0))
					{
						StoppingToken.ThrowIfCancellationRequested();
						await Task.Delay(TimeSpan.FromSeconds(1.0), StoppingToken);
					}
				}
			}
			catch (AbandonedMutexException)
			{
			}
		}

		/// <summary>
		/// Handles the lifetime of an agent session.
		/// </summary>
		/// <param name="StoppingToken">Indicates that the service is trying to stop</param>
		/// <returns>Async task</returns>
		async Task HandleSessionAsync(CancellationToken StoppingToken)
		{
			// Make sure there's only one instance of the agent running
			using Mutex SingleInstanceMutex = new Mutex(false, "Global\\HordeAgent-DB828ACB-0AA5-4D32-A62A-21D4429B1014");
			await WaitForMutexAsync(SingleInstanceMutex, StoppingToken);

			// Terminate any remaining child processes from other instances
			TerminateProcesses(Logger, StoppingToken);

			// Show the worker capabilities
			AgentCapabilities Capabilities = await GetAgentCapabilities(WorkingDir, Logger);
			if (Capabilities.Properties.Count > 0)
			{
				Logger.LogInformation("Global:");
				foreach (string Property in Capabilities.Properties)
				{
					Logger.LogInformation("  {AgentProperty}", Property);
				}
			}
			foreach (DeviceCapabilities Device in Capabilities.Devices)
			{
				Logger.LogInformation($"{Device.Handle} Device:");
				foreach (string Property in Device.Properties)
				{
					Logger.LogInformation("   {DeviceProperty}", Property);
				}
			}

			// Mount all the necessary network shares. Currently only supported on Windows.
			if (Settings.ShareMountingEnabled && RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				foreach (MountNetworkShare Share in Settings.Shares)
				{
					if (Share.MountPoint != null && Share.RemotePath != null)
					{
						Logger.LogInformation("Mounting {RemotePath} as {MountPoint}", Share.RemotePath, Share.MountPoint);
						NetworkShare.Mount(Share.MountPoint, Share.RemotePath);
					}
				}
			}

			// Create the session
			CreateSessionResponse CreateSessionResponse;
			using (GrpcChannel Channel = GrpcService.CreateGrpcChannel(ServerProfile.Token))
			{
				HordeRpc.HordeRpcClient RpcClient = new HordeRpc.HordeRpcClient(Channel);

				// Create the session information
				CreateSessionRequest SessionRequest = new CreateSessionRequest();
				SessionRequest.Name = Settings.GetAgentName();
				SessionRequest.Status = AgentStatus.Ok;
				SessionRequest.Capabilities = Capabilities;
				SessionRequest.Version = Program.Version;

				// Create a session
				CreateSessionResponse = await RpcClient.CreateSessionAsync(SessionRequest, null, null, StoppingToken);
				Logger.LogInformation("Session started ({SessionId})", CreateSessionResponse.SessionId);
			}

			// Open a connection to the server
			await using (IRpcConnection RpcCon = RpcConnection.Create(() => GrpcService.CreateGrpcChannel(CreateSessionResponse.Token), Logger))
			{
				// Loop until we're ready to exit
				Stopwatch UpdateCapabilitiesTimer = Stopwatch.StartNew();
				for (; ; )
				{
					Task WaitTask = UpdateLeasesEvent.Task;

					// Flag for whether the service is stopping
					bool bStopping = StoppingToken.IsCancellationRequested || RequestShutdown;

					// Build the next update request
					UpdateSessionRequest UpdateSessionRequest = new UpdateSessionRequest();
					UpdateSessionRequest.AgentId = CreateSessionResponse.AgentId;
					UpdateSessionRequest.SessionId = CreateSessionResponse.SessionId;

					// Get the new the lease states. If a restart is requested and we have no active leases, signal to the server that we're stopping.
					lock (LockObject)
					{
						foreach (LeaseInfo LeaseInfo in ActiveLeases)
						{
							UpdateSessionRequest.Leases.Add(new Lease(LeaseInfo.Lease));
						}
						if (RequestShutdown && ActiveLeases.Count == 0)
						{
							bStopping = true;
						}
					}

					// Get the new agent status
					if (bStopping)
					{
						UpdateSessionRequest.Status = AgentStatus.Stopping;
					}
					else if (Unhealthy)
					{
						UpdateSessionRequest.Status = AgentStatus.Unhealthy;
					}
					else
					{
						UpdateSessionRequest.Status = AgentStatus.Ok;
					}


					// Update the capabilities every 5m
					if (UpdateCapabilitiesTimer.Elapsed > TimeSpan.FromMinutes(5.0))
					{
						UpdateSessionRequest.Capabilities = await GetAgentCapabilities(WorkingDir, Logger);
						UpdateCapabilitiesTimer.Restart();
					}

					// Complete the wait task if we subsequently stop
					using (bStopping ? (CancellationTokenRegistration?)null : StoppingToken.Register(() => UpdateLeasesEvent.Set()))
					{
						// Update the state with the server
						UpdateSessionResponse? UpdateSessionResponse = null;
						using (IRpcClientRef? RpcClientRef = RpcCon.TryGetClientRef(new RpcContext()))
						{
							if (RpcClientRef == null)
							{
								await Task.WhenAny(Task.Delay(TimeSpan.FromSeconds(5.0)), WaitTask);
							}
							else
							{
								UpdateSessionResponse = await UpdateSessionAsync(RpcClientRef, UpdateSessionRequest, WaitTask);
							}
						}

						lock (LockObject)
						{
							// Now reconcile the local state to match what the server reports
							if (UpdateSessionResponse != null)
							{
								// Remove any leases which have completed
								ActiveLeases.RemoveAll(x => (x.Lease.State == LeaseState.Completed || x.Lease.State == LeaseState.Cancelled) && !UpdateSessionResponse.Leases.Any(y => y.Id == x.Lease.Id && y.State != LeaseState.Cancelled));

								// Create any new leases and cancel any running leases
								foreach (Lease ServerLease in UpdateSessionResponse.Leases)
								{
									if (ServerLease.State == LeaseState.Cancelled)
									{
										LeaseInfo? Info = ActiveLeases.FirstOrDefault(x => x.Lease.Id == ServerLease.Id);
										if (Info != null)
										{
											Logger.LogInformation("Cancelling lease {LeaseId}", ServerLease.Id);
											Info.CancellationTokenSource.Cancel();
										}
									}
									if (ServerLease.State == LeaseState.Pending && !ActiveLeases.Any(x => x.Lease.Id == ServerLease.Id))
									{
										ServerLease.State = LeaseState.Active;

										Logger.LogInformation("Adding lease {LeaseId}", ServerLease.Id);
										LeaseInfo Info = new LeaseInfo(ServerLease);
										Info.Task = Task.Run(() => HandleLeaseAsync(RpcCon, CreateSessionResponse.AgentId, Info));
										ActiveLeases.Add(Info);
									}
								}
							}

							// If there's nothing still running and cancellation was requested, exit
							if (ActiveLeases.Count == 0 && UpdateSessionRequest.Status == AgentStatus.Stopping)
							{
								Logger.LogInformation("No leases are active. Agent is stopping.");
								break;
							}
						}
					}
				}

				Logger.LogInformation("Disposing RpcConnection");
			}
		}

		/// <summary>
		/// Wrapper for <see cref="UpdateSessionInternalAsync"/> which filters/logs exceptions
		/// </summary>
		/// <param name="RpcClientRef">The RPC client connection</param>
		/// <param name="UpdateSessionRequest">The session update request</param>
		/// <param name="WaitTask">Task which can be used to jump out of the update early</param>
		/// <returns>Response from the call</returns>
		async Task<UpdateSessionResponse?> UpdateSessionAsync(IRpcClientRef RpcClientRef, UpdateSessionRequest UpdateSessionRequest, Task WaitTask)
		{
			UpdateSessionResponse? UpdateSessionResponse = null;
			try
			{
				UpdateSessionResponse = await UpdateSessionInternalAsync(RpcClientRef, UpdateSessionRequest, WaitTask);
				UpdateSessionFailures = 0;
			}
			catch (RpcException Ex)
			{
				if (++UpdateSessionFailures >= 3)
				{
					throw;
				}
				else if (Ex.StatusCode == StatusCode.Unavailable)
				{
					Logger.LogInformation(Ex, "Service unavailable while calling UpdateSessionAsync(), will retry");
				}
				else
				{
					Logger.LogError(Ex, "Error while executing RPC. Will retry.");
				}
			}
			return UpdateSessionResponse;
		}

		/// <summary>
		/// Tries to update the session state on the server.
		/// 
		/// This operation is a little gnarly due to the fact that we want to long-poll for the result.
		/// Since we're doing the update via a gRPC call, the way to do that without using cancellation tokens is to keep the request stream open
		/// until we want to terminate the call (see https://github.com/grpc/grpc/issues/8277). In order to do that, we need to make a 
		/// bidirectional streaming call, even though we only expect one response/response.
		/// </summary>
		/// <param name="RpcClientRef">The RPC client</param>
		/// <param name="Request">The session update request</param>
		/// <param name="WaitTask">Task to use to terminate the wait</param>
		/// <returns>The response object</returns>
		async Task<UpdateSessionResponse?> UpdateSessionInternalAsync(IRpcClientRef RpcClientRef, UpdateSessionRequest Request, Task WaitTask)
		{
			DateTime Deadline = DateTime.UtcNow + TimeSpan.FromMinutes(2.0);
			using (AsyncDuplexStreamingCall<UpdateSessionRequest, UpdateSessionResponse> Call = RpcClientRef.Client.UpdateSession(deadline: Deadline))
			{
				Logger.LogDebug("Updating session {SessionId} (Status={Status})", Request.SessionId, Request.Status);

				// Write the request to the server
				await Call.RequestStream.WriteAsync(Request);

				// Wait until the server responds or we need to trigger a new update
				Task<bool> MoveNextAsync = Call.ResponseStream.MoveNext();

				Task Task = await Task.WhenAny(MoveNextAsync, WaitTask, RpcClientRef.DisposingTask);
				if(Task == WaitTask)
				{
					Logger.LogDebug("Cancelling long poll from client side (new update)");
				}
				else if (Task == RpcClientRef.DisposingTask)
				{
					Logger.LogDebug("Cancelling long poll from client side (server migration)");
				}

				// Close the request stream to indicate that we're finished
				await Call.RequestStream.CompleteAsync();

				// Wait for a response or a new update to come in, then close the request stream
				UpdateSessionResponse? Response = null;
				while (await MoveNextAsync)
				{
					Response = Call.ResponseStream.Current;
					MoveNextAsync = Call.ResponseStream.MoveNext();
				}
				return Response;
			}
		}

		/// <summary>
		/// Handle a lease request
		/// </summary>
		/// <param name="RpcConnection">The RPC connection to the server</param>
		/// <param name="AgentId">The agent id</param>
		/// <param name="LeaseInfo">Information about the lease</param>
		/// <returns>Async task</returns>
		async Task HandleLeaseAsync(IRpcConnection RpcConnection, string AgentId, LeaseInfo LeaseInfo)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("HandleLease").WithResourceName(LeaseInfo.Lease.Id).StartActive();
			Scope.Span.SetTag("LeaseId", LeaseInfo.Lease.Id);
			Scope.Span.SetTag("AgentId", AgentId);
//			using IDisposable TraceProperty = LogContext.PushProperty("dd.trace_id", CorrelationIdentifier.TraceId.ToString());
//			using IDisposable SpanProperty = LogContext.PushProperty("dd.span_id", CorrelationIdentifier.SpanId.ToString());

			Logger.LogInformation("Handling lease {LeaseId}", LeaseInfo.Lease.Id);

			// Get the lease outcome
			LeaseResult Result = LeaseResult.Failed;
			try
			{
				Result = await HandleLeasePayloadAsync(RpcConnection, AgentId, LeaseInfo);
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Unhandled exception while executing lease {LeaseId}", LeaseInfo.Lease.Id);
			}

			// Update the state of the lease
			lock (LockObject)
			{
				if (LeaseInfo.CancellationTokenSource.IsCancellationRequested)
				{
					LeaseInfo.Lease.State = LeaseState.Cancelled;
					LeaseInfo.Lease.Outcome = LeaseOutcome.Failed;
					LeaseInfo.Lease.Output = ByteString.Empty;
				}
				else
				{
					LeaseInfo.Lease.State = (Result.Outcome == LeaseOutcome.Cancelled) ? LeaseState.Cancelled : LeaseState.Completed;
					LeaseInfo.Lease.Outcome = Result.Outcome;
					LeaseInfo.Lease.Output = (Result.Output != null)? ByteString.CopyFrom(Result.Output) : ByteString.Empty;
				}
				Logger.LogInformation("Transitioning lease {LeaseId} to {State}, outcome={Outcome}", LeaseInfo.Lease.Id, LeaseInfo.Lease.State, LeaseInfo.Lease.Outcome);
			}
			UpdateLeasesEvent.Set();
		}

		/// <summary>
		/// Dispatch a lease payload to the appropriate handler
		/// </summary>
		/// <param name="RpcConnection">The RPC connection to the server</param>
		/// <param name="AgentId">The current agent id</param>
		/// <param name="LeaseInfo">Information about the lease</param>
		/// <returns>Outcome from the lease</returns>
		internal async Task<LeaseResult> HandleLeasePayloadAsync(IRpcConnection RpcConnection, string AgentId, LeaseInfo LeaseInfo)
		{
			Any Payload = LeaseInfo.Lease.Payload;

			ComputeTaskMessage ComputeTask;
			if (LeaseInfo.Lease.Payload.TryUnpack(out ComputeTask))
			{
				GlobalTracer.Instance.ActiveSpan?.SetTag("task", "Compute");
				return await ComputeAsync(RpcConnection, LeaseInfo.Lease.Id, ComputeTask, Logger, LeaseInfo.CancellationTokenSource.Token);
			}

			ConformTask ConformTask;
			if (LeaseInfo.Lease.Payload.TryUnpack(out ConformTask))
			{
				GlobalTracer.Instance.ActiveSpan?.SetTag("task", "Conform");
				Func<ILogger, Task<LeaseResult>> Handler = NewLogger => ConformAsync(RpcConnection, AgentId, LeaseInfo.Lease.Id, ConformTask, NewLogger, LeaseInfo.CancellationTokenSource.Token);
				return await HandleLeasePayloadWithLogAsync(RpcConnection, ConformTask.LogId, AgentId, null, null, Handler, LeaseInfo.CancellationTokenSource.Token);
			}

			ExecuteJobTask JobTask;
			if (LeaseInfo.Lease.Payload.TryUnpack(out JobTask))
			{
				GlobalTracer.Instance.ActiveSpan?.SetTag("task", "Job");
				Func<ILogger, Task<LeaseResult>> Handler = NewLogger => ExecuteJobAsync(RpcConnection, AgentId, LeaseInfo.Lease.Id, JobTask, NewLogger, LeaseInfo.CancellationTokenSource.Token);
				return await HandleLeasePayloadWithLogAsync(RpcConnection, JobTask.LogId, AgentId, JobTask.JobId, JobTask.BatchId, Handler, LeaseInfo.CancellationTokenSource.Token);
			}

			UpgradeTask UpgradeTask;
			if (LeaseInfo.Lease.Payload.TryUnpack(out UpgradeTask) || TryUnpackUpgradeTask(LeaseInfo.Lease.Payload, out UpgradeTask))
			{
				GlobalTracer.Instance.ActiveSpan?.SetTag("task", "Upgrade");
				Func<ILogger, Task<LeaseResult>> Handler = NewLogger => UpgradeAsync(RpcConnection, AgentId, UpgradeTask, NewLogger, LeaseInfo.CancellationTokenSource.Token);
				return await HandleLeasePayloadWithLogAsync(RpcConnection, UpgradeTask.LogId, AgentId, null, null, Handler, LeaseInfo.CancellationTokenSource.Token);
			}

			ShutdownTask ShutdownTask;
			if (LeaseInfo.Lease.Payload.TryUnpack(out ShutdownTask))
			{
				GlobalTracer.Instance.ActiveSpan?.SetTag("task", "Shutdown");
				Func<ILogger, Task<LeaseResult>> Handler = NewLogger => ShutdownAsync(RpcConnection, AgentId, ShutdownTask, NewLogger, LeaseInfo.CancellationTokenSource.Token);
				return await HandleLeasePayloadWithLogAsync(RpcConnection, ShutdownTask.LogId, AgentId, null, null, Handler, LeaseInfo.CancellationTokenSource.Token);
			}

			RestartTask RestartTask;
			if (LeaseInfo.Lease.Payload.TryUnpack(out RestartTask))
			{
				GlobalTracer.Instance.ActiveSpan?.SetTag("task", "Restart");
				Func<ILogger, Task<LeaseResult>> Handler = NewLogger => RestartAsync(RpcConnection, AgentId, RestartTask, NewLogger, LeaseInfo.CancellationTokenSource.Token);
				return await HandleLeasePayloadWithLogAsync(RpcConnection, RestartTask.LogId, AgentId, null, null, Handler, LeaseInfo.CancellationTokenSource.Token);
			}

			Logger.LogError("Invalid lease payload type ({PayloadType})", Payload.TypeUrl);
			return LeaseResult.Failed;
		}

		/// <summary>
		/// Attempts to parse an upgrade task, given either name
		/// </summary>
		/// <param name="Payload"></param>
		/// <param name="UpgradeTask"></param>
		/// <returns></returns>
		static bool TryUnpackUpgradeTask(Any Payload, out UpgradeTask UpgradeTask)
		{
			if (Payload.TypeUrl == "type.googleapis.com/UpgradeTask" || Payload.TypeUrl == "type.googleapis.com/Horde.UpgradeTask")
			{
				UpgradeTask = UpgradeTask.Parser.ParseFrom(Payload.Value);
				return true;
			}
			else
			{
				UpgradeTask = null!;
				return false;
			}
		}

		/// <summary>
		/// Terminates any processes which are still running under the given directory
		/// </summary>
		/// <param name="Logger">Logger device</param>
		/// <param name="CancellationToken">Cancellation token</param>
		void TerminateProcesses(ILogger Logger, CancellationToken CancellationToken)
		{
			// Terminate child processes from any previous runs
			ProcessUtils.TerminateProcesses(ShouldTerminateProcess, Logger, CancellationToken);
		}

		/// <summary>
		/// Callback for determining whether a process should be terminated
		/// </summary>
		/// <param name="ImageFile">The file to terminate</param>
		/// <returns>True if the process should be terminated</returns>
		bool ShouldTerminateProcess(FileReference ImageFile)
		{
			if (ImageFile.IsUnderDirectory(WorkingDir))
			{
				return true;
			}

			string FileName = ImageFile.GetFileName();
			if (ProcessNamesToTerminate.Contains(FileName))
			{
				return true;
			}

			return false;
		}

		/// <summary>
		/// Dispatch a lease payload to the appropriate handler
		/// </summary>
		/// <param name="RpcConnection">The RPC connection to the server</param>
		/// <param name="LogId">Unique id for the log to create</param>
		/// <param name="AgentId">The current agent id</param>
		/// <param name="JobId">The job being executed</param>
		/// <param name="JobBatchId">The batch being executed</param>
		/// <param name="ExecuteLease">Action to perform to execute the lease</param>
		/// <param name="CancellationToken">The cancellation token for this lease</param>
		/// <returns>Outcome from the lease</returns>
		async Task<LeaseResult> HandleLeasePayloadWithLogAsync(IRpcConnection RpcConnection, string LogId, string AgentId, string? JobId, string? JobBatchId, Func<ILogger, Task<LeaseResult>> ExecuteLease, CancellationToken CancellationToken)
		{
			await using (JsonRpcLogger? ConformLogger = String.IsNullOrEmpty(LogId) ? null : new JsonRpcLogger(RpcConnection, LogId, JobId, JobBatchId, null, null, Logger))
			{
				ILogger LeaseLogger = (ConformLogger == null) ? (ILogger)Logger : new ForwardingLogger(Logger, new DefaultLoggerIndentHandler(ConformLogger));
				try
				{
					LeaseResult Result = await ExecuteLease(LeaseLogger);
					return Result;
				}
				catch (Exception Ex)
				{
					if (CancellationToken.IsCancellationRequested && IsCancellationException(Ex))
					{
						LeaseLogger.LogInformation("Lease was cancelled");
						return LeaseResult.Cancelled;
					}
					else
					{
						LeaseLogger.LogError(Ex, "Caught unhandled exception while attempting to execute lease:\n{Exception}", Ex);
						return LeaseResult.Failed;
					}
				}
			}
		}

		/// <summary>
		/// Execute a remote execution aciton
		/// </summary>
		/// <param name="RpcConnection">RPC client for communicating with the server</param>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="ComputeTask">The action task parameters</param>
		/// <param name="Logger">Logger for the task</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns></returns>
		internal async Task<LeaseResult> ComputeAsync(IRpcConnection RpcConnection, string LeaseId, ComputeTaskMessage ComputeTask, ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Starting compute task (lease {LeaseId})", LeaseId);

			ComputeTaskResultMessage Result;
			try
			{
				DateTimeOffset ActionTaskStartTime = DateTimeOffset.UtcNow;
				using (IRpcClientRef Client = await RpcConnection.GetClientRef(new RpcContext(), CancellationToken))
				{
					DirectoryReference LeaseDir = DirectoryReference.Combine(WorkingDir, "Compute", LeaseId);
					DirectoryReference.CreateDirectory(LeaseDir);

					ComputeTaskExecutor Executor = new ComputeTaskExecutor(Client.Channel, Logger);
					try
					{
						Result = await Executor.ExecuteAsync(LeaseId, ComputeTask, LeaseDir, CancellationToken);
					}
					finally
					{
						try
						{
							DirectoryReference.Delete(LeaseDir, true);
						}
						catch
						{
						}
					}
				}
			}
			catch (BlobNotFoundException Ex)
			{
				Logger.LogError(Ex, "Blob not found: {Hash}", Ex.Hash);
				Result = new ComputeTaskResultMessage(ComputeTaskOutcome.BlobNotFound, Ex.Hash.ToString());
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Exception while executing compute task");
				Result = new ComputeTaskResultMessage(ComputeTaskOutcome.Exception, Ex.ToString());
			}

			return new LeaseResult(Result.ToByteArray());
		}

		/// <summary>
		/// Conform a machine
		/// </summary>
		/// <param name="RpcConnection">RPC client for communicating with the server</param>
		/// <param name="AgentId">The current agent id</param>
		/// <param name="LeaseId">The current lease id</param>
		/// <param name="ConformTask">The conform task parameters</param>
		/// <param name="ConformLogger">Logger for the task</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Async task</returns>
		async Task<LeaseResult> ConformAsync(IRpcConnection RpcConnection, string AgentId, string LeaseId, ConformTask ConformTask, ILogger ConformLogger, CancellationToken CancellationToken)
		{
			ConformLogger.LogInformation("Conforming, lease {LeaseId}", LeaseId);
			TerminateProcesses(ConformLogger, CancellationToken);

			IList<AgentWorkspace> PendingWorkspaces = ConformTask.Workspaces;
			for (; ;)
			{
				// Run the conform task
				if (Settings.Executor == ExecutorType.Perforce && Settings.PerforceExecutor.RunConform)
				{
					await PerforceExecutor.ConformAsync(WorkingDir, PendingWorkspaces, ConformLogger, CancellationToken);
				}
				else
				{
					ConformLogger.LogInformation("Skipping due to Settings.RunConform flag");
				}

				// Update the new set of workspaces
				UpdateAgentWorkspacesRequest Request = new UpdateAgentWorkspacesRequest();
				Request.AgentId = AgentId;
				Request.Workspaces.AddRange(PendingWorkspaces);

				UpdateAgentWorkspacesResponse Response = await RpcConnection.InvokeAsync(x => x.UpdateAgentWorkspacesAsync(Request, null, null, CancellationToken), new RpcContext(), CancellationToken);
				if (!Response.Retry)
				{
					ConformLogger.LogInformation("Conform finished");
					break;
				}

				ConformLogger.LogInformation("Pending workspaces have changed - running conform again...");
				PendingWorkspaces = Response.PendingWorkspaces;
			}

			return LeaseResult.Success;
		}

		/// <summary>
		/// Execute part of a job
		/// </summary>
		/// <param name="RpcClient">RPC client for communicating with the server</param>
		/// <param name="AgentId">The current agent id</param>
		/// <param name="LeaseId">The current lease id</param>
		/// <param name="ExecuteTask">The task to execute</param>
		/// <param name="Logger">The logger to use for this lease</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Async task</returns>
		internal async Task<LeaseResult> ExecuteJobAsync(IRpcConnection RpcClient, string AgentId, string LeaseId, ExecuteJobTask ExecuteTask, ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Executing job \"{JobName}\", jobId {JobId}, batchId {BatchId}, leaseId {LeaseId}", ExecuteTask.JobName, ExecuteTask.JobId, ExecuteTask.BatchId, LeaseId);
			GlobalTracer.Instance.ActiveSpan?.SetTag("jobId", ExecuteTask.JobId.ToString());
			GlobalTracer.Instance.ActiveSpan?.SetTag("jobName", ExecuteTask.JobName.ToString());
			GlobalTracer.Instance.ActiveSpan?.SetTag("batchId", ExecuteTask.BatchId.ToString());	

			// Start executing the current batch
			BeginBatchResponse Batch = await RpcClient.InvokeAsync(x => x.BeginBatchAsync(new BeginBatchRequest(ExecuteTask.JobId, ExecuteTask.BatchId, LeaseId), null, null, CancellationToken), new RpcContext(), CancellationToken);

			// Execute the batch
			try
			{
				await ExecuteBatchAsync(RpcClient, AgentId, LeaseId, ExecuteTask, Batch, Logger, CancellationToken);
			}
			catch (Exception Ex)
			{
				if (CancellationToken.IsCancellationRequested && IsCancellationException(Ex))
				{
					throw;
				}
				else
				{
					Logger.LogError(Ex, $"Exception while executing batch: {Ex}");
				}
			}

			// Mark the batch as complete
			await RpcClient.InvokeAsync(x => x.FinishBatchAsync(new FinishBatchRequest(ExecuteTask.JobId, ExecuteTask.BatchId, LeaseId), null, null, CancellationToken), new RpcContext(), CancellationToken);
			Logger.LogInformation("Done.");

			return LeaseResult.Success;
		}

		/// <summary>
		/// Executes a batch
		/// </summary>
		/// <param name="RpcClient">RPC client for communicating with the server</param>
		/// <param name="AgentId">The current agent id</param>
		/// <param name="LeaseId">The current lease id</param>
		/// <param name="ExecuteTask">The task to execute</param>
		/// <param name="Batch">The batch to execute</param>
		/// <param name="BatchLogger">Output log for the batch</param>
		/// <param name="CancellationToken">Cancellation token to abort the batch</param>
		/// <returns>Async task</returns>
		async Task ExecuteBatchAsync(IRpcConnection RpcClient, string AgentId, string LeaseId, ExecuteJobTask ExecuteTask, BeginBatchResponse Batch, ILogger BatchLogger, CancellationToken CancellationToken)
		{
			BatchLogger.LogInformation("Executing batch {BatchId} using {Executor} executor", ExecuteTask.BatchId, Settings.Executor.ToString());
			TerminateProcesses(BatchLogger, CancellationToken);

			// Get the working directory for this lease
			DirectoryReference ScratchDir = DirectoryReference.Combine(WorkingDir, "Scratch");

			// Create an executor for this job
			IExecutor Executor = CreateExecutor(RpcClient, ExecuteTask, Batch);

			// Try to initialize the executor
			BatchLogger.LogInformation("Initializing...");
			using (BatchLogger.BeginIndentScope("  "))
			{
				using IScope Scope = GlobalTracer.Instance.BuildSpan("Initialize").StartActive();
				await Executor.InitializeAsync(BatchLogger, CancellationToken);
			}

			// Execute the steps
			for (; ; )
			{
				// Get the next step to execute
				BeginStepResponse Step = await RpcClient.InvokeAsync(x => x.BeginStepAsync(new BeginStepRequest(ExecuteTask.JobId, ExecuteTask.BatchId, LeaseId), null, null, CancellationToken), new RpcContext(), CancellationToken);
				if (Step.State == BeginStepResponse.Types.Result.Waiting)
				{
					BatchLogger.LogInformation("Waiting for dependency to be ready");
					await Task.Delay(TimeSpan.FromSeconds(20.0), CancellationToken);
					continue;
				}
				else if (Step.State == BeginStepResponse.Types.Result.Complete)
				{
					break;
				}
				else if (Step.State != BeginStepResponse.Types.Result.Ready)
				{
					BatchLogger.LogError("Unexpected step state: {StepState}", Step.State);
					break;
				}

				// Print the new state
				Stopwatch StepTimer = Stopwatch.StartNew();
				BatchLogger.LogInformation("Starting job {JobId}, batch {BatchId}, step {StepId}", ExecuteTask.JobId, ExecuteTask.BatchId, Step.StepId);

				// Create a trace span
				using IScope Scope = GlobalTracer.Instance.BuildSpan("Execute").WithResourceName(Step.Name).StartActive();
				Scope.Span.SetTag("stepId", Step.StepId);
				Scope.Span.SetTag("logId", Step.LogId);
//				using IDisposable TraceProperty = LogContext.PushProperty("dd.trace_id", CorrelationIdentifier.TraceId.ToString());
//				using IDisposable SpanProperty = LogContext.PushProperty("dd.span_id", CorrelationIdentifier.SpanId.ToString());

				// Update the context to include information about this step
				JobStepOutcome StepOutcome;
				JobStepState StepState;
				using (BatchLogger.BeginIndentScope("  "))
				{
					// Start writing to the log file
					await using (JsonRpcLogger StepLogger = new JsonRpcLogger(RpcClient, Step.LogId, ExecuteTask.JobId, ExecuteTask.BatchId, Step.StepId, Step.Warnings, Logger))
					{
						// Execute the task
						ILogger ForwardingLogger = new DefaultLoggerIndentHandler(StepLogger);
						if (Settings.WriteStepOutputToLogger)
						{
							ForwardingLogger = new ForwardingLogger(Logger, ForwardingLogger);
						}

						using CancellationTokenSource StepPollCancelSource = new CancellationTokenSource();
						using CancellationTokenSource StepAbortSource = new CancellationTokenSource();
						TaskCompletionSource<bool> StepFinishedSource = new TaskCompletionSource<bool>();
						Task StepPollTask = Task.Run(() => PollForStepAbort(RpcClient, ExecuteTask.JobId, ExecuteTask.BatchId, Step.StepId, StepPollCancelSource.Token, StepAbortSource, StepFinishedSource.Task));
						
						try
						{
							(StepOutcome, StepState) = await ExecuteStepAsync(Executor, Step, ForwardingLogger, CancellationToken, StepAbortSource.Token);
						}
						finally
						{
							// Will get called even when cancellation token for the lease/batch fires
							StepFinishedSource.SetResult(true); // Tell background poll task to stop
							await StepPollTask;
						}
						
						// Kill any processes spawned by the step
						TerminateProcesses(StepLogger, CancellationToken);

						// Wait for the logger to finish
						await StepLogger.StopAsync();
						
						// Reflect the warnings/errors in the step outcome
						if (StepOutcome > StepLogger.Outcome)
						{
							StepOutcome = StepLogger.Outcome;
						}
					}

					// Update the server with the outcome from the step
					BatchLogger.LogInformation("Marking step as complete (Outcome={Outcome}, State={StepState})", StepOutcome, StepState);
					await RpcClient.InvokeAsync(x => x.UpdateStepAsync(new UpdateStepRequest(ExecuteTask.JobId, ExecuteTask.BatchId, Step.StepId, StepState, StepOutcome), null, null, CancellationToken), new RpcContext(), CancellationToken);
				}

				// Print the finishing state
				StepTimer.Stop();
				BatchLogger.LogInformation("Completed in {Time}", StepTimer.Elapsed);
			}

			// Clean the environment
			BatchLogger.LogInformation("Finalizing...");
			using (BatchLogger.BeginIndentScope("  "))
			{
				using IScope Scope = GlobalTracer.Instance.BuildSpan("Finalize").StartActive();
				await Executor.FinalizeAsync(BatchLogger, CancellationToken);
			}
		}

		/// <summary>
		/// Executes a step
		/// </summary>
		/// <param name="Executor">The executor to run this step</param>
		/// <param name="Step">Step to execute</param>
		/// <param name="StepLogger">Logger for the step</param>
		/// <param name="CancellationToken">Cancellation token to abort the batch</param>
		/// <param name="StepCancellationToken">Cancellation token to abort only this individual step</param>
		/// <returns>Async task</returns>
		internal async Task<(JobStepOutcome, JobStepState)> ExecuteStepAsync(IExecutor Executor, BeginStepResponse Step, ILogger StepLogger, CancellationToken CancellationToken, CancellationToken StepCancellationToken)
		{
			using CancellationTokenSource Combined = CancellationTokenSource.CreateLinkedTokenSource(CancellationToken, StepCancellationToken);
			try
			{
				JobStepOutcome StepOutcome = await Executor.RunAsync(Step, StepLogger, Combined.Token);
				return (StepOutcome, JobStepState.Completed);
			}
			catch (Exception Ex)
			{
				if (CancellationToken.IsCancellationRequested && IsCancellationException(Ex))
				{
					StepLogger.LogError("The step was cancelled by batch/lease");
					throw;
				}
				
				if (StepCancellationToken.IsCancellationRequested && IsCancellationException(Ex))
				{
					StepLogger.LogError("The step was intentionally cancelled");
					return (JobStepOutcome.Failure, JobStepState.Aborted);
				}
				
				StepLogger.LogError(Ex, $"Exception while executing step: {Ex}");
				return (JobStepOutcome.Failure, JobStepState.Completed);
			}
		}

		internal async Task PollForStepAbort(IRpcConnection RpcClient, string JobId, string BatchId, string StepId, CancellationToken CancellationToken, CancellationTokenSource StepCancelSource, Task FinishedTask)
		{
			Stopwatch Timer = Stopwatch.StartNew();
			while (!FinishedTask.IsCompleted)
			{
				try
				{
					if (Timer.Elapsed > TimeSpan.FromHours(24.0))
					{
						Logger.LogDebug("Step was aborted after running for {NumHours} (JobId={JobId} BatchId={BatchId} StepId={StepId})", Timer.Elapsed, JobId, BatchId, StepId);
						StepCancelSource.Cancel();
						break;
					}

					GetStepResponse Res = await RpcClient.InvokeAsync(x => x.GetStepAsync(new GetStepRequest(JobId, BatchId, StepId), null, null, CancellationToken), new RpcContext(), CancellationToken);
					if (Res.AbortRequested)
					{
						Logger.LogDebug("Step was aborted by server (JobId={JobId} BatchId={BatchId} StepId={StepId})", JobId, BatchId, StepId);
						StepCancelSource.Cancel();
						break;
					}
				}
				catch (RpcException Ex)
				{
					Logger.LogError(Ex, "Poll for step abort has failed. Aborting (JobId={JobId} BatchId={BatchId} StepId={StepId})", JobId, BatchId, StepId);
					StepCancelSource.Cancel();
					break;
				}

				await Task.WhenAny(Task.Delay(StepAbortPollInterval), FinishedTask);
			}
		}

		/// <summary>
		/// Determine if the given exception was triggered due to a cancellation event
		/// </summary>
		/// <param name="Ex">The exception to check</param>
		/// <returns>True if the exception is a cancellation exception</returns>
		static bool IsCancellationException(Exception Ex)
		{
			if(Ex is OperationCanceledException)
			{
				return true;
			}

			RpcException? RpcException = Ex as RpcException;
			if(RpcException != null && RpcException.StatusCode == StatusCode.Cancelled)
			{
				return true;
			}

			return false;
		}

		/// <summary>
		/// Check for an update of the agent software
		/// </summary>
		/// <param name="RpcClient">RPC client for communicating with the server</param>
		/// <param name="AgentId">The current agent id</param>
		/// <param name="UpgradeTask">The upgrade task</param>
		/// <param name="Logger">Logging device</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Outcome of this operation</returns>
		async Task<LeaseResult> UpgradeAsync(IRpcConnection RpcClient, string AgentId, UpgradeTask UpgradeTask, ILogger Logger, CancellationToken CancellationToken)
		{
			string RequiredVersion = UpgradeTask.SoftwareId;

			// Check if we're running the right version
			if (RequiredVersion != null && RequiredVersion != Program.Version)
			{
				Logger.LogInformation("Upgrading from {CurrentVersion} to {TargetVersion}", Program.Version, RequiredVersion);

				// Clear out the working directory
				DirectoryReference UpgradeDir = DirectoryReference.Combine(WorkingDir, "Upgrade");
				DirectoryReference.CreateDirectory(UpgradeDir);
				await DeleteDirectoryContentsAsync(new DirectoryInfo(UpgradeDir.FullName));

				// Download the new software
				FileInfo OutputFile = new FileInfo(Path.Combine(UpgradeDir.FullName, "Agent.zip"));
				using (IRpcClientRef RpcClientRef = await RpcClient.GetClientRef(new RpcContext(), CancellationToken))
				using (AsyncServerStreamingCall<DownloadSoftwareResponse> Cursor = RpcClientRef.Client.DownloadSoftware(new DownloadSoftwareRequest(RequiredVersion), null, null, CancellationToken))
				{
					using (Stream OutputStream = OutputFile.Open(FileMode.Create))
					{
						while (await Cursor.ResponseStream.MoveNext(CancellationToken))
						{
							OutputStream.Write(Cursor.ResponseStream.Current.Data.Span);
						}
					}
				}

				// Extract it to a temporary directory
				DirectoryReference ExtractedDir = DirectoryReference.Combine(UpgradeDir, "Extracted");
				DirectoryReference.CreateDirectory(ExtractedDir);
				ZipFile.ExtractToDirectory(OutputFile.FullName, ExtractedDir.FullName);

				//				// Debug code for updating an agent with the local version
				//				foreach (FileInfo SourceFile in new FileInfo(Assembly.GetExecutingAssembly().Location).Directory.EnumerateFiles())
				//				{
				//					SourceFile.CopyTo(Path.Combine(ExtractedDir.FullName, SourceFile.Name), true);
				//				}


				// Get the current process and assembly. This may be different if running through dotnet.exe rather than a native PE image.
				FileReference AssemblyFileName = new FileReference(Assembly.GetExecutingAssembly().Location);

				// Spawn the other process
				using (Process Process = new Process())
				{
					StringBuilder Arguments = new StringBuilder();

					DirectoryReference TargetDir = AssemblyFileName.Directory;

					// We were launched via an external application (presumably dotnet.exe). Do the same thing again.
					FileReference NewAssemblyFileName = FileReference.Combine(ExtractedDir, AssemblyFileName.MakeRelativeTo(TargetDir));
					if (!FileReference.Exists(NewAssemblyFileName))
					{
						Logger.LogError("Unable to find {AgentExe} in extracted archive", NewAssemblyFileName);
						return LeaseResult.Failed;
					}

					Process.StartInfo.FileName = "dotnet";

					StringBuilder CurrentArguments = new StringBuilder();
					foreach (string Arg in Program.Args)
					{
						CurrentArguments.AppendArgument(Arg);
					}

					Arguments.AppendArgument(NewAssemblyFileName.FullName);
					Arguments.AppendArgument("Service");
					Arguments.AppendArgument("Upgrade");
					Arguments.AppendArgument("-ProcessId=", Process.GetCurrentProcess().Id.ToString());
					Arguments.AppendArgument("-TargetDir=", TargetDir.FullName);
					Arguments.AppendArgument("-Arguments=", CurrentArguments.ToString());

					Process.StartInfo.Arguments = Arguments.ToString();
					Process.StartInfo.UseShellExecute = false;
					Process.EnableRaisingEvents = true;

					StringBuilder LaunchCommand = new StringBuilder();
					LaunchCommand.AppendArgument(Process.StartInfo.FileName);
					LaunchCommand.Append(' ');
					LaunchCommand.Append(Arguments);
					Logger.LogInformation("Launching: {Launch}", LaunchCommand.ToString());

					TaskCompletionSource<int> ExitCodeSource = new TaskCompletionSource<int>();
					Process.Exited += (Sender, Args) => { ExitCodeSource.SetResult(Process.ExitCode); };

					Process.Start();

					using (CancellationToken.Register(() => { ExitCodeSource.SetResult(0); }))
					{
						await ExitCodeSource.Task;
					}
				}
			}

			return LeaseResult.Success;
		}

		/// <summary>
		/// Delete the contents of a directory without deleting it itself
		/// </summary>
		/// <param name="BaseDir">Directory to clean</param>
		/// <returns>Async task</returns>
		static async Task DeleteDirectoryContentsAsync(DirectoryInfo BaseDir)
		{
			List<Task> ChildTasks = new List<Task>();
			foreach (DirectoryInfo SubDir in BaseDir.EnumerateDirectories())
			{
				ChildTasks.Add(Task.Run(() => DeleteDirectory(SubDir)));
			}
			foreach (FileInfo File in BaseDir.EnumerateFiles())
			{
				File.Attributes = FileAttributes.Normal;
				File.Delete();
			}
			await Task.WhenAll(ChildTasks);
		}

		/// <summary>
		/// Deletes a directory and its contents
		/// </summary>
		/// <param name="BaseDir">Directory to delete</param>
		/// <returns>Async task</returns>
		static async Task DeleteDirectory(DirectoryInfo BaseDir)
		{
			await DeleteDirectoryContentsAsync(BaseDir);
			BaseDir.Delete();
		}

		/// <summary>
		/// Check for an update of the agent software
		/// </summary>
		/// <param name="RpcClient">RPC client for communicating with the server</param>
		/// <param name="AgentId">The current agent id</param>
		/// <param name="RestartTask">The restart task parameters</param>
		/// <param name="Logger">Logging device</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Outcome of this operation</returns>
		Task<LeaseResult> RestartAsync(IRpcConnection RpcClient, string AgentId, RestartTask RestartTask, ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Setting restart flag");
			RequestShutdown = true;
			RestartAfterShutdown = true;
			return Task.FromResult(LeaseResult.Success);
		}

		/// <summary>
		/// Shutdown the agent
		/// </summary>
		/// <param name="RpcClient">RPC client for communicating with the server</param>
		/// <param name="AgentId">The current agent id</param>
		/// <param name="ShutdownTask">The restart task parameters</param>
		/// <param name="Logger">Logging device</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Outcome of this operation</returns>
		Task<LeaseResult> ShutdownAsync(IRpcConnection RpcClient, string AgentId, ShutdownTask ShutdownTask, ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Setting shutdown flag");
			RequestShutdown = true;
			RestartAfterShutdown = false;
			return Task.FromResult(LeaseResult.Success);
		}

		/// <summary>
		/// Gets the hardware capabilities of this worker
		/// </summary>
		/// <returns>Worker object for advertising to the server</returns>
		static async Task<AgentCapabilities> GetAgentCapabilities(DirectoryReference WorkingDir, ILogger Logger)
		{
			// Create the primary device
			DeviceCapabilities PrimaryDevice = new DeviceCapabilities();
			PrimaryDevice.Handle = "Primary";

			List<DeviceCapabilities> OtherDevices = new List<DeviceCapabilities>();
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				PrimaryDevice.Properties.Add("Platform=Win64");
				PrimaryDevice.Properties.Add("PlatformGroup=Windows");
				PrimaryDevice.Properties.Add("PlatformGroup=Microsoft");
				PrimaryDevice.Properties.Add("PlatformGroup=Desktop");

				PrimaryDevice.Properties.Add("OSFamily=Windows");

				// Add OS info
				using (ManagementObjectSearcher Searcher = new ManagementObjectSearcher("select * from Win32_OperatingSystem"))
				{
					foreach (ManagementObject Row in Searcher.Get())
					{
						using (Row)
						{
							Dictionary<string, object> Properties = GetWmiProperties(Row);

							object? Name;
							if (Properties.TryGetValue("Caption", out Name))
							{
								PrimaryDevice.Properties.Add($"OSDistribution={Name}");
							}

							object? Version;
							if (Properties.TryGetValue("Version", out Version))
							{
								PrimaryDevice.Properties.Add($"OSKernelVersion={Version}");
							}
						}
					}
				}

				// Add CPU info
				using (ManagementObjectSearcher Searcher = new ManagementObjectSearcher("select * from Win32_Processor"))
				{
					Dictionary<string, int> NameToCount = new Dictionary<string, int>();
					int TotalPhysicalCores = 0;
					int TotalLogicalCores = 0;

					foreach (ManagementObject Row in Searcher.Get())
					{
						using (Row)
						{
							Dictionary<string, object> Properties = GetWmiProperties(Row);

							object? NameObject;
							if (Properties.TryGetValue("Name", out NameObject))
							{
								string Name = NameObject.ToString() ?? String.Empty;
								int Count;
								NameToCount.TryGetValue(Name, out Count);
								NameToCount[Name] = Count + 1;
							}

							object? NumPhysicalCores;
							if ((Properties.TryGetValue("NumberOfEnabledCore", out NumPhysicalCores) && NumPhysicalCores is uint) || (Properties.TryGetValue("NumberOfCores", out NumPhysicalCores) && NumPhysicalCores is uint))
							{
								TotalPhysicalCores += (int)(uint)NumPhysicalCores;
							}

							object? NumLogicalCores;
							if (Properties.TryGetValue("NumberOfLogicalProcessors", out NumLogicalCores) && NumLogicalCores is uint)
							{
								TotalLogicalCores += (int)(uint)NumLogicalCores;
							}
						}
					}

					if (NameToCount.Count > 0)
					{
						PrimaryDevice.Properties.Add("CPU=" + String.Join(", ", NameToCount.Select(x => (x.Value > 1) ? $"{x.Key} x {x.Value}" : x.Key)));
					}

					if (TotalLogicalCores > 0)
					{
						PrimaryDevice.Properties.Add($"LogicalCores={TotalLogicalCores}");
					}

					if (TotalPhysicalCores > 0)
					{
						PrimaryDevice.Properties.Add($"PhysicalCores={TotalPhysicalCores}");
					}
				}

				// Add RAM info
				using (ManagementObjectSearcher Searcher = new ManagementObjectSearcher("select Capacity from Win32_PhysicalMemory"))
				{
					ulong TotalCapacity = 0;
					foreach (ManagementObject Row in Searcher.Get())
					{
						using (Row)
						{
							object? Capacity = Row.GetPropertyValue("Capacity");
							if (Capacity is ulong)
							{
								TotalCapacity += (ulong)Capacity;
							}
						}
					}

					if (TotalCapacity > 0)
					{
						PrimaryDevice.Properties.Add($"RAM={TotalCapacity / (1024 * 1024 * 1024)}");
					}
				}

				// Add GPU info
				using (ManagementObjectSearcher Searcher = new ManagementObjectSearcher("select Name, DriverVersion, AdapterRAM from Win32_VideoController"))
				{
					int Index = 0;
					foreach (ManagementObject Row in Searcher.Get())
					{
						using (Row)
						{
							WmiProperties Properties = new WmiProperties(Row);
							if (Properties.TryGetValue("Name", out string? Name) && Properties.TryGetValue("DriverVersion", out string? DriverVersion))
							{
								DeviceCapabilities Device = new DeviceCapabilities();
								Device.Handle = $"GPU-{++Index}";
								Device.Properties.Add($"Name={Name}");
								Device.Properties.Add($"Type=GPU");
								Device.Properties.Add($"DriverVersion={DriverVersion}");
								OtherDevices.Add(Device);
							}
						}
					}
				}

				// Add EC2 properties if needed
				await AddAwsProperties(PrimaryDevice.Properties, Logger);

				// Add session information
				PrimaryDevice.Properties.Add($"User={Environment.UserName}");
				PrimaryDevice.Properties.Add($"Domain={Environment.UserDomainName}");
				PrimaryDevice.Properties.Add($"Interactive={Environment.UserInteractive}");
				PrimaryDevice.Properties.Add($"Elevated={BuildGraphExecutor.IsUserAdministrator()}");
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				PrimaryDevice.Properties.Add("Platform=Linux");
				PrimaryDevice.Properties.Add("PlatformGroup=Linux");
				PrimaryDevice.Properties.Add("PlatformGroup=Unix");
				PrimaryDevice.Properties.Add("PlatformGroup=Desktop");

				PrimaryDevice.Properties.Add("OSFamily=Linux");
				PrimaryDevice.Properties.Add("OSVersion=Linux");

				// Add EC2 properties if needed
				await AddAwsProperties(PrimaryDevice.Properties, Logger);

				// Add session information
				PrimaryDevice.Properties.Add($"User={Environment.UserName}");
				PrimaryDevice.Properties.Add($"Domain={Environment.UserDomainName}");
				PrimaryDevice.Properties.Add($"Interactive={Environment.UserInteractive}");
				PrimaryDevice.Properties.Add($"Elevated={BuildGraphExecutor.IsUserAdministrator()}");
			}
			else if(RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			{
				PrimaryDevice.Properties.Add("Platform=Mac");
				PrimaryDevice.Properties.Add("PlatformGroup=Apple");
				PrimaryDevice.Properties.Add("PlatformGroup=Desktop");

				PrimaryDevice.Properties.Add("OSFamily=MacOS");
				PrimaryDevice.Properties.Add("OSVersion=MacOS");

				string Output;
				using(Process Process = new Process())
				{
					Process.StartInfo.FileName = "system_profiler";
					Process.StartInfo.Arguments = "SPHardwareDataType SPSoftwareDataType -xml";
					Process.StartInfo.CreateNoWindow = true;
					Process.StartInfo.RedirectStandardOutput = true;
					Process.StartInfo.RedirectStandardInput = false;
					Process.StartInfo.UseShellExecute = false;
					Process.Start();

					Output = Process.StandardOutput.ReadToEnd();
				}

				XmlDocument Xml = new XmlDocument();
				Xml.LoadXml(Output);

				XmlNode? HardwareNode = Xml.SelectSingleNode("/plist/array/dict/key[. = '_dataType']/following-sibling::string[. = 'SPHardwareDataType']/../key[. = '_items']/following-sibling::array/dict");
				if(HardwareNode != null)
				{
					XmlNode? Model = HardwareNode.SelectSingleNode("key[. = 'machine_model']/following-sibling::string");
					if(Model != null)
					{
						PrimaryDevice.Properties.Add($"Model={Model.InnerText}");
					}

					XmlNode? CpuTypeNode = HardwareNode.SelectSingleNode("key[. = 'cpu_type']/following-sibling::string");
					XmlNode? CpuSpeedNode = HardwareNode.SelectSingleNode("key[. = 'current_processor_speed']/following-sibling::string");
					XmlNode? CpuPackagesNode = HardwareNode.SelectSingleNode("key[. = 'packages']/following-sibling::integer");
					if(CpuTypeNode != null && CpuSpeedNode != null && CpuPackagesNode != null)
					{
						PrimaryDevice.Properties.Add((CpuPackagesNode.InnerText != "1")? $"CPU={CpuPackagesNode.InnerText} x {CpuTypeNode.InnerText} @ {CpuSpeedNode.InnerText}" : $"CPU={CpuTypeNode.InnerText} @ {CpuSpeedNode.InnerText}");
					}

					PrimaryDevice.Properties.Add($"LogicalCores={Environment.ProcessorCount}");

					XmlNode? CpuCountNode = HardwareNode.SelectSingleNode("key[. = 'number_processors']/following-sibling::integer");
					if(CpuCountNode != null)
					{
						PrimaryDevice.Properties.Add($"PhysicalCores={CpuCountNode.InnerText}");
					}

					XmlNode? MemoryNode = HardwareNode.SelectSingleNode("key[. = 'physical_memory']/following-sibling::string");
					if(MemoryNode != null)
					{
						string[] Parts = MemoryNode.InnerText.Split(new char[]{' '}, StringSplitOptions.RemoveEmptyEntries);
						if(Parts.Length == 2 && Parts[1] == "GB")
						{
							PrimaryDevice.Properties.Add($"RAM={Parts[0]}");
						}
					}
				}

				XmlNode? SoftwareNode = Xml.SelectSingleNode("/plist/array/dict/key[. = '_dataType']/following-sibling::string[. = 'SPSoftwareDataType']/../key[. = '_items']/following-sibling::array/dict");
				if(SoftwareNode != null)
				{
					XmlNode? OsVersionNode = SoftwareNode.SelectSingleNode("key[. = 'os_version']/following-sibling::string");
					if(OsVersionNode != null)
					{
						PrimaryDevice.Properties.Add($"OSDistribution={OsVersionNode.InnerText}");
					}

					XmlNode? KernelVersionNode = SoftwareNode.SelectSingleNode("key[. = 'kernel_version']/following-sibling::string");
					if(KernelVersionNode != null)
					{
						PrimaryDevice.Properties.Add($"OSKernelVersion={KernelVersionNode.InnerText}");
					}
				}
			}

			// Get the IP addresses
			try
			{
				IPHostEntry Entry = Dns.GetHostEntry(Dns.GetHostName());
				foreach (IPAddress Address in Entry.AddressList)
				{
					if (Address.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork)
					{
						PrimaryDevice.Properties.Add($"Ipv4={Address}");
					}
					else if (Address.AddressFamily == System.Net.Sockets.AddressFamily.InterNetworkV6)
					{
						PrimaryDevice.Properties.Add($"Ipv6={Address}");
					}
				}
			}
			catch (Exception Ex)
			{
				Logger.LogDebug(Ex, "Unable to get local IP address");
			}

			// Get the time that the machine booted
			PrimaryDevice.Properties.Add($"BootTime={BootTime}");
			PrimaryDevice.Properties.Add($"StartTime={StartTime}");

			// Add disk info based on platform
			string? DriveName;
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				DriveName = Path.GetPathRoot(WorkingDir.FullName);
			}
			else
			{
				DriveName = WorkingDir.FullName;
			}

			if (DriveName != null)
			{
				try
				{
					DriveInfo Info = new DriveInfo(DriveName);
					PrimaryDevice.Properties.Add($"DiskFreeSpace={Info.AvailableFreeSpace}");
					PrimaryDevice.Properties.Add($"DiskTotalSize={Info.TotalSize}");
				}
				catch (Exception Ex)
				{
					Logger.LogWarning(Ex, "Unable to query disk info for path '{DriveName}'", DriveName);
				}
			}
			PrimaryDevice.Properties.Add($"WorkingDir={WorkingDir}");

			// Add any horde. env vars for custom properties.
			IEnumerable<string> EnvVars = Environment.GetEnvironmentVariables().Keys.Cast<string>();
			foreach (string EnvVar in EnvVars.Where(x => x.StartsWith("horde.", StringComparison.InvariantCultureIgnoreCase)))
			{
				PrimaryDevice.Properties.Add($"{EnvVar}={Environment.GetEnvironmentVariable(EnvVar)}");
			}

			// Create the worker
			AgentCapabilities Agent = new AgentCapabilities();
			Agent.Devices.Add(PrimaryDevice);
			Agent.Devices.AddRange(OtherDevices);
			return Agent;
		}

		class WmiProperties
		{
			Dictionary<string, object> Properties = new Dictionary<string, object>(StringComparer.Ordinal);

			public WmiProperties(ManagementObject Row)
			{
				foreach (PropertyData Property in Row.Properties)
				{
					Properties[Property.Name] = Property.Value;
				}
			}

			public bool TryGetValue(string Name, [NotNullWhen(true)] out string? Value)
			{
				object? Object;
				if (Properties.TryGetValue(Name, out Object))
				{
					Value = Object.ToString();
					return Value != null;
				}
				else
				{
					Value = null!;
					return false;
				}
			}

			public bool TryGetValue(string Name, out long Value)
			{
				object? Object;
				if (Properties.TryGetValue(Name, out Object))
				{
					if (Object is int)
					{
						Value = (int)Object;
						return true;
					}
					else if (Object is uint)
					{
						Value = (uint)Object;
						return true;
					}
					else if (Object is long)
					{
						Value = (long)Object;
						return true;
					}
					else if (Object is ulong)
					{
						Value = (long)(ulong)Object;
						return true;
					}
				}

				Value = 0;
				return false;
			}
		}

		static Dictionary<string, object> GetWmiProperties(ManagementObject Row)
		{
			Dictionary<string, object> Properties = new Dictionary<string, object>(StringComparer.Ordinal);
			foreach (PropertyData Property in Row.Properties)
			{
				Properties[Property.Name] = Property.Value;
			}
			return Properties;
		}

		static async Task AddAwsProperties(IList<string> Properties, ILogger Logger)
		{
			if (EC2InstanceMetadata.IdentityDocument != null)
			{
				Properties.Add("EC2=1");
				AddAwsProperty("aws-instance-id", "/instance-id", Properties);
				AddAwsProperty("aws-instance-type", "/instance-type", Properties);
				AddAwsProperty("aws-region", "/region", Properties);

				try
				{
					using (AmazonEC2Client Client = new AmazonEC2Client())
					{
						DescribeTagsRequest Request = new DescribeTagsRequest();
						Request.Filters = new List<Filter>();
						Request.Filters.Add(new Filter("resource-id", new List<string> { EC2InstanceMetadata.InstanceId }));

						DescribeTagsResponse Response = await Client.DescribeTagsAsync(Request);
						foreach (TagDescription Tag in Response.Tags)
						{
							Properties.Add($"aws-tag={Tag.Key}:{Tag.Value}");
						}
					}
				}
				catch (Exception Ex)
				{
					Logger.LogDebug(Ex, "Unable to query EC2 tags.");
				}
			}
		}			

		static void AddAwsProperty(string Name, string AwsKey, IList<string> Properties)
		{
			string? Value = EC2InstanceMetadata.GetData(AwsKey);
			if (Value != null)
			{
				Properties.Add($"{Name}={Value}");
			}
		}
	}
}
