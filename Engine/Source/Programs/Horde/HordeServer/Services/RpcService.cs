// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using HordeCommon;
using HordeCommon.Rpc;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Net;
using System.Runtime.CompilerServices;
using System.Security.Claims;
using System.Text;
using System.Threading.Tasks;
using AgentSoftwareChannelName = HordeServer.Utilities.StringId<HordeServer.Services.AgentSoftwareChannels>;
using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;
using IStream = HordeServer.Models.IStream;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using HordeServer.Tasks.Impl;

[assembly: InternalsVisibleTo("HordeAgentTests")]

namespace HordeServer.Services
{
	/// <summary>
	/// Implements the Horde gRPC service for bots updating their status and dequeing work
	/// </summary>
	[Authorize]
	public class RpcService : HordeRpc.HordeRpcBase
	{
		/// <summary>
		/// Timeout before closing a long-polling request (client will retry again) 
		/// </summary>
		internal TimeSpan LongPollTimeout = TimeSpan.FromMinutes(9);

		/// <summary>
		/// Instance of the DatabaseService singleton
		/// </summary>
		DatabaseService DatabaseService;

		/// <summary>
		/// Instance of the AclService singleton
		/// </summary>
		AclService AclService;

		/// <summary>
		/// Instance of the AgentService singleton
		/// </summary>
		AgentService AgentService;

		/// <summary>
		/// The stream service instance
		/// </summary>
		StreamService StreamService;

		/// <summary>
		/// The job service instance
		/// </summary>
		JobService JobService;

		/// <summary>
		/// The software service instance
		/// </summary>
		AgentSoftwareService AgentSoftwareService;

		/// <summary>
		/// The artifact service instance
		/// </summary>
		ArtifactService ArtifactService;

		/// <summary>
		/// The log file service instance
		/// </summary>
		ILogFileService LogFileService;

		/// <summary>
		/// The credential service instance
		/// </summary>
		CredentialService CredentialService;

		/// <summary>
		/// The pool service instance
		/// </summary>
		PoolService PoolService;

		/// <summary>
		/// The application lifetime interface
		/// </summary>
		LifetimeService LifetimeService;

		/// <summary>
		/// Collection of graph documents
		/// </summary>
		IGraphCollection Graphs;

		/// <summary>
		/// Collection of testdata documents
		/// </summary>
		ITestDataCollection TestData;

		/// <summary>
		/// The RPC action source
		/// </summary>
		ActionTaskSource ActionTaskSource;

		/// <summary>
		/// The conform task source
		/// </summary>
		ConformTaskSource ConformTaskSource;

		/// <summary>
		/// Writer for log output
		/// </summary>
		ILogger<RpcService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">Instance of the DatabaseService singleton</param>
		/// <param name="AclService">Instance of the AclService singleton</param>
		/// <param name="AgentService">Instance of the AgentService singleton</param>
		/// <param name="JobService">Instance of the JobService singleton</param>
		/// <param name="StreamService">Instance of the StreamService singleton</param>
		/// <param name="LogFileService">Instance of the LogFileService singleton</param>
		/// <param name="AgentSoftwareService">Instance of the JobService singleton</param>
		/// <param name="ArtifactService">Instance of the ArtifactService singleton</param>
		/// <param name="CredentialService">Instance of the CredentialsService singleton</param>
		/// <param name="PoolService">Instance of the PoolService singleton</param>
		/// <param name="LifetimeService">The application lifetime</param>
		/// <param name="Graphs">Collection of graph documents</param>
		/// <param name="TestData">Collection of testdata</param>
		/// <param name="ActionTaskSource"></param>
		/// <param name="ConformTaskSource"></param>
		/// <param name="Logger">Log writer</param>
		public RpcService(DatabaseService DatabaseService, AclService AclService, AgentService AgentService, StreamService StreamService, JobService JobService, AgentSoftwareService AgentSoftwareService, ArtifactService ArtifactService, ILogFileService LogFileService, CredentialService CredentialService, PoolService PoolService, LifetimeService LifetimeService, IGraphCollection Graphs, ITestDataCollection TestData, ActionTaskSource ActionTaskSource, ConformTaskSource ConformTaskSource, ILogger<RpcService> Logger)
		{
			this.DatabaseService = DatabaseService;
			this.AclService = AclService;
			this.AgentService = AgentService;
			this.StreamService = StreamService;
			this.JobService = JobService;
			this.AgentSoftwareService = AgentSoftwareService;
			this.ArtifactService = ArtifactService;
			this.LogFileService = LogFileService;
			this.CredentialService = CredentialService;
			this.PoolService = PoolService;
			this.LifetimeService = LifetimeService;
			this.Graphs = Graphs;
			this.TestData = TestData;
			this.ActionTaskSource = ActionTaskSource;
			this.ConformTaskSource = ConformTaskSource;
			this.Logger = Logger;
		}

		/// <summary>
		/// Waits until the server is terminating
		/// </summary>
		/// <param name="Reader">Request reader</param>
		/// <param name="Writer">Response writer</param>
		/// <param name="Context">Context for the call</param>
		/// <returns>Response object</returns>
		public override async Task QueryServerState(IAsyncStreamReader<QueryServerStateRequest> Reader, IServerStreamWriter<QueryServerStateResponse> Writer, ServerCallContext Context)
		{
			if (await Reader.MoveNext())
			{
				QueryServerStateRequest Request = Reader.Current;
				Logger.LogInformation("Start server query for client {Name}", Request.Name);

				// Return the current response
				QueryServerStateResponse Response = new QueryServerStateResponse();
				Response.Name = Dns.GetHostName();
				await Writer.WriteAsync(Response);

				// Move to the next request from the client. This should always be the end of the stream, but will not occur until the client stops requesting responses.
				Task<bool> MoveNextTask = Reader.MoveNext();

				// Wait for the client to close the stream or a shutdown to start
				Task LongPollDelay = Task.Delay(LongPollTimeout);
				Task WaitTask = await Task.WhenAny(MoveNextTask, LifetimeService.StoppingTask, LongPollDelay);

				if (WaitTask == MoveNextTask)
				{
					throw new Exception("Unexpected request to QueryServerState posted from client.");
				}
				else if (WaitTask == LifetimeService.StoppingTask)
				{
					Logger.LogInformation("Notifying client {Name} of server shutdown", Request.Name);
					await Writer.WriteAsync(Response);
				}
				else if (WaitTask == LongPollDelay)
				{
					// Send same response as server shutdown. In the agent perspective, they will be identical.
					await Writer.WriteAsync(Response);
				}
			}
		}

		/// <summary>
		/// Waits until the server is terminating
		/// </summary>
		/// <param name="Reader">Request reader</param>
		/// <param name="Writer">Response writer</param>
		/// <param name="Context">Context for the call</param>
		/// <returns>Response object</returns>
		public override async Task QueryServerStateV2(IAsyncStreamReader<QueryServerStateRequest> Reader, IServerStreamWriter<QueryServerStateResponse> Writer, ServerCallContext Context)
		{
			if (await Reader.MoveNext())
			{
				QueryServerStateRequest Request = Reader.Current;
				Logger.LogInformation("Start server query for client {Name}", Request.Name);

				try
				{
					// Return the current response
					QueryServerStateResponse Response = new QueryServerStateResponse();
					Response.Name = Dns.GetHostName();
					Response.Stopping = LifetimeService.IsStopping;
					await Writer.WriteAsync(Response);

					// Move to the next request from the client. This should always be the end of the stream, but will not occur until the client stops requesting responses.
					Task<bool> MoveNextTask = Reader.MoveNext();

					// Wait for the client to close the stream or a shutdown to start
					if (await Task.WhenAny(MoveNextTask, LifetimeService.StoppingTask) == LifetimeService.StoppingTask)
					{
						Response.Stopping = true;
						await Writer.WriteAsync(Response);
					}

					// Wait until the client has finished sending
					while (await MoveNextTask)
					{
						MoveNextTask = Reader.MoveNext();
					}
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Exception in QueryServerState for {Name}", Request.Name);
					throw;
				}
			}
		}

		/// <summary>
		/// Updates the workspaces synced for an agent
		/// </summary>
		/// <param name="Request">The request parameters</param>
		/// <param name="Context">Context for the call</param>
		/// <returns>Response object</returns>
		public override async Task<UpdateAgentWorkspacesResponse> UpdateAgentWorkspaces(UpdateAgentWorkspacesRequest Request, ServerCallContext Context)
		{
			for (; ; )
			{
				// Get the current agent state
				IAgent? Agent = await AgentService.GetAgentAsync(new AgentId(Request.AgentId));
				if (Agent == null)
				{
					throw new StructuredRpcException(StatusCode.OutOfRange, "Agent {AgentId} does not exist", Request.AgentId);
				}

				// Get the new workspaces
				List<AgentWorkspace> NewWorkspaces = Request.Workspaces.Select(x => new AgentWorkspace(x)).ToList();

				// Get the set of workspaces that are currently required
				HashSet<AgentWorkspace> ConformWorkspaces = await PoolService.GetWorkspacesAsync(Agent, DateTime.UtcNow);
				bool bPendingConform = !ConformWorkspaces.SetEquals(NewWorkspaces);

				// Update the workspaces
				if (await AgentService.TryUpdateWorkspacesAsync(Agent, NewWorkspaces, bPendingConform))
				{
					UpdateAgentWorkspacesResponse Response = new UpdateAgentWorkspacesResponse();
					if (bPendingConform)
					{
						Response.Retry = await ConformTaskSource.GetWorkspacesAsync(Agent, Response.PendingWorkspaces);
					}
					return Response;
				}
			}
		}

		/// <summary>
		/// Creates a new session
		/// </summary>
		/// <param name="Request">Request to create a new agent</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<CreateSessionResponse> CreateSession(CreateSessionRequest Request, ServerCallContext Context)
		{
			if (Request.Capabilities == null)
			{
				throw new StructuredRpcException(StatusCode.InvalidArgument, "Capabilities may not be null");
			}

			AgentId AgentId = new AgentId(Request.Name);
			using IDisposable Scope = Logger.BeginScope("CreateSession({AgentId})", AgentId.ToString());

			// Find the agent
			IAgent? Agent = await AgentService.GetAgentAsync(AgentId);
			if (Agent == null)
			{
				if (!await AclService.AuthorizeAsync(AclAction.CreateAgent, Context.GetHttpContext().User))
				{
					throw new StructuredRpcException(StatusCode.PermissionDenied, "User is not authenticated to create new agents");
				}

				const bool bEnabled = true;
				const bool bEphemeral = false;
				Agent = await AgentService.CreateAgentAsync(Request.Name, bEnabled, bEphemeral, null, null);
			}

			// Make sure we're allowed to create sessions on this agent
			if (!await AgentService.AuthorizeAsync(Agent, AclAction.CreateSession, Context.GetHttpContext().User, null))
			{
				throw new StructuredRpcException(StatusCode.PermissionDenied, "User is not authenticated to create session for {AgentId}", Request.Name);
			}

			// Create a new session
			Agent = await AgentService.CreateSessionAsync(Agent, Request.Status, new AgentCapabilities(Request.Capabilities), AgentSoftwareVersion.FromNullable(Request.Version));
			if (Agent == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Agent {AgentId} not found", Request.Name);
			}

			// Create the response
			CreateSessionResponse Response = new CreateSessionResponse();
			Response.AgentId = Agent.Id.ToString();
			Response.SessionId = Agent.SessionId.ToString();
			Response.ExpiryTime = Timestamp.FromDateTime(Agent.SessionExpiresAt!.Value);
			Response.Token = AgentService.IssueSessionToken(Agent.SessionId!.Value);
			return Response;
		}

		/// <summary>
		/// Updates an agent session
		/// </summary>
		/// <param name="Reader">Request to create a new agent</param>
		/// <param name="Writer">Writer for response objects</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task UpdateSession(IAsyncStreamReader<UpdateSessionRequest> Reader, IServerStreamWriter<UpdateSessionResponse> Writer, ServerCallContext Context)
		{
			// Read the request object
			Task<bool> NextRequestTask = Reader.MoveNext();
			if (await NextRequestTask)
			{
				UpdateSessionRequest Request = Reader.Current;
				using IDisposable Scope = Logger.BeginScope("UpdateSession for agent {AgentId}, session {SessionId}", Request.AgentId, Request.SessionId);

				Logger.LogDebug("Updating session for {AgentId}", Request.AgentId);
				foreach (HordeCommon.Rpc.Messages.Lease Lease in Request.Leases)
				{
					Logger.LogDebug("Session {SessionId}, Lease {LeaseId} - State: {LeaseState}, Outcome: {LeaseOutcome}", Request.SessionId, Lease.Id, Lease.State, Lease.Outcome);
				}

				// Get a task for moving to the next item. This will only complete once the call has closed.
				NextRequestTask = Reader.MoveNext();

				// Get the current agent state
				IAgent? Agent = await AgentService.GetAgentAsync(new AgentId(Request.AgentId));
				if(Agent != null)
				{
					// Check we're authorized to update it
					if (!AgentService.AuthorizeSession(Agent, Context.GetHttpContext().User))
					{
						throw new StructuredRpcException(StatusCode.PermissionDenied, "Not authenticated for {AgentId}", Request.AgentId);
					}

					// Get the new capabilities of this agent
					AgentCapabilities? Capabilities = null;
					if (Request.Capabilities != null)
					{
						Capabilities = new AgentCapabilities(Request.Capabilities);
					}

					// Capture the current agent update index. This allows us to detect if anything has changed.
					uint UpdateIndex = Agent.UpdateIndex;

					// Update the agent session and return to the caller if anything changes
					Agent = await AgentService.UpdateSessionAsync(Agent, Request.SessionId.ToObjectId(), Request.Status, Capabilities, Request.Leases);
					if (Agent != null && Agent.UpdateIndex == UpdateIndex && (Agent.Leases.Count > 0 || Agent.Status != AgentStatus.Stopping))
					{
						Agent = await AgentService.WaitForLeaseAsync(Agent, NextRequestTask);
					}
				}

				// Handle the invalid agent case
				if (Agent == null)
				{
					throw new StructuredRpcException(StatusCode.NotFound, "Invalid agent name '{AgentId}'", Request.AgentId);
				}

				// Create the new session info
				UpdateSessionResponse Response = new UpdateSessionResponse();
				Response.Leases.Add(Agent.Leases.Select(x => x.ToRpcMessage()));
				Response.ExpiryTime = (Agent.SessionExpiresAt == null) ? new Timestamp() : Timestamp.FromDateTime(Agent.SessionExpiresAt.Value);
				await Writer.WriteAsync(Response);

				// Wait for the client to close the stream
				while (await NextRequestTask)
				{
					NextRequestTask = Reader.MoveNext();
				}
			}
		}

		/// <summary>
		/// Gets information about a stream
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public override async Task<GetStreamResponse> GetStream(GetStreamRequest Request, ServerCallContext Context)
		{
			StreamId StreamIdValue = new StreamId(Request.StreamId);

			IStream? Stream = await StreamService.GetStreamAsync(StreamIdValue);
			if (Stream == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Stream {StreamId} does not exist", Request.StreamId);
			}
			if (!await StreamService.AuthorizeAsync(Stream, AclAction.ViewStream, Context.GetHttpContext().User, null))
			{
				throw new StructuredRpcException(StatusCode.PermissionDenied, "Not authenticated to access stream {StreamId}", Request.StreamId);
			}

			return Stream.ToRpcResponse();
		}

		/// <summary>
		/// Gets information about a job
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<GetJobResponse> GetJob(GetJobRequest Request, ServerCallContext Context)
		{
			ObjectId JobIdValue = Request.JobId.ToObjectId();

			IJob? Job = await JobService.GetJobAsync(JobIdValue);
			if (Job == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Job {JobId} does not exist", Request.JobId);
			}
			if (!JobService.AuthorizeSession(Job, Context.GetHttpContext().User))
			{
				throw new StructuredRpcException(StatusCode.PermissionDenied, "Not authenticated to access job {JobId}", Request.JobId);
			}

			return Job.ToRpcResponse();
		}

		/// <summary>
		/// Updates properties on a job
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<Empty> UpdateJob(UpdateJobRequest Request, ServerCallContext Context)
		{
			ObjectId JobIdValue = Request.JobId.ToObjectId();

			IJob? Job = await JobService.GetJobAsync(JobIdValue);
			if (Job == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Job {JobId} does not exist", Request.JobId);
			}
			if (!JobService.AuthorizeSession(Job, Context.GetHttpContext().User))
			{
				throw new StructuredRpcException(StatusCode.PermissionDenied, "Not authenticated to modify job {JobId}", Request.JobId);
			}

			await JobService.UpdateJobAsync(Job, Name: Request.Name);
			return new Empty();
		}

		/// <summary>
		/// Starts executing a batch
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<BeginBatchResponse> BeginBatch(BeginBatchRequest Request, ServerCallContext Context)
		{
			ObjectId JobId = Request.JobId.ToObjectId();
			SubResourceId BatchId = Request.BatchId.ToSubResourceId();

			IJob? Job = await JobService.GetJobAsync(Request.JobId.ToObjectId());
			if (Job == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Job {JobId} not found", Request.JobId);
			}

			IJobStepBatch Batch = AuthorizeBatch(Job, Request.BatchId.ToSubResourceId(), Context);
			if (!await JobService.UpdateBatchAsync(Job, BatchId, null, Api.JobStepBatchState.Running))
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Batch {JobId}:{BatchId} not found for updating", Request.JobId, Request.BatchId);
			}

			IGraph Graph = await JobService.GetGraphAsync(Job);

			BeginBatchResponse Response = new BeginBatchResponse();
			Response.LogId = Batch.LogId.ToString();
			Response.AgentType = Graph.Groups[Batch.GroupIdx].AgentType;
			return Response;
		}

		/// <summary>
		/// Finishes executing a batch
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<Empty> FinishBatch(FinishBatchRequest Request, ServerCallContext Context)
		{
			IJob? Job = await JobService.GetJobAsync(Request.JobId.ToObjectId());
			if (Job == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Job {JobId} not found", Request.JobId);
			}

			IJobStepBatch Batch = AuthorizeBatch(Job, Request.BatchId.ToSubResourceId(), Context);
			await JobService.UpdateBatchAsync(Job, Batch.Id, null, Api.JobStepBatchState.Complete);
			return new Empty();
		}

		/// <summary>
		/// Starts executing a step
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<BeginStepResponse> BeginStep(BeginStepRequest Request, ServerCallContext Context)
		{
			// Log file that's allocated for this step. We only need do this once.
			ILogFile? Log = null;

			// Get the job
			for (; ; )
			{
				// Check the job exists and we can access it
				IJob? Job = await JobService.GetJobAsync(Request.JobId.ToObjectId());
				if (Job == null)
				{
					throw new StructuredRpcException(StatusCode.NotFound, "Job {JobId} not found", Request.JobId);
				}

				// Find the batch being executed
				IJobStepBatch Batch = AuthorizeBatch(Job, Request.BatchId.ToSubResourceId(), Context);

				// Figure out which step to execute next
				IJobStep? Step;
				for (int StepIdx = 0; ; StepIdx++)
				{
					// If there aren't any more steps, send a complete message
					if (StepIdx == Batch.Steps.Count)
					{
						BeginStepResponse Response = new BeginStepResponse();
						Response.State = BeginStepResponse.Types.Result.Complete;
						return Response;
					}

					// Check if this step is ready to be executed
					Step = Batch.Steps[StepIdx];
					if (Step.State == JobStepState.Ready)
					{
						break;
					}
					if (Step.State == JobStepState.Waiting)
					{
						BeginStepResponse Response = new BeginStepResponse();
						Response.State = BeginStepResponse.Types.Result.Waiting;
						return Response;
					}
				}

				// Create a log file if necessary
				if (Log == null)
				{
					Log = await LogFileService.CreateLogFileAsync(Job.Id, Batch.SessionId, Api.LogType.Json);
				}

				// Get the node for this step
				IGraph Graph = await JobService.GetGraphAsync(Job);
				INode Node = Graph.Groups[Batch.GroupIdx].Nodes[Step.NodeIdx];

				// Figure out all the credentials for it (and check we can access them)
				Dictionary<string, string> Credentials = new Dictionary<string, string>();
				//				if (Node.Credentials != null)
				//				{
				//					ClaimsPrincipal Principal = new ClaimsPrincipal(new ClaimsIdentity(Job.Claims.Select(x => new Claim(x.Type, x.Value))));
				//					if (!await GetCredentialsForStep(Principal, Node, Credentials, Message => FailStep(Job, Batch.Id, Step.Id, Log, Message)))
				//					{
				//						Log = null;
				//						continue;
				//					}
				//				}

				// Update the step state
				if (await JobService.TryUpdateStepAsync(Job, Batch.Id, Step.Id, JobStepState.Running, JobStepOutcome.Unspecified, null, null, Log.Id, null, null, null, null))
				{
					BeginStepResponse Response = new BeginStepResponse();
					Response.State = BeginStepResponse.Types.Result.Ready;
					Response.LogId = Log.Id.ToString();
					Response.StepId = Step.Id.ToString();
					Response.Name = Node.Name;
					Response.Credentials.Add(Credentials);
					if (Node.Properties != null)
					{
						Response.Properties.Add(Node.Properties);
					}
					Response.Warnings = Node.Warnings;
					return Response;
				}
			}
		}

		/// <summary>
		/// Gets all the required credentials for the given step
		/// </summary>
		/// <param name="User">The user to validate</param>
		/// <param name="Node">The node being executed</param>
		/// <param name="Credentials">Receives a list of credentials for the step</param>
		/// <param name="WriteError">Delegate used to write error messages</param>
		/// <returns>Async task</returns>
		private async Task<bool> GetCredentialsForStep(ClaimsPrincipal User, INode Node, Dictionary<string, string> Credentials, Func<string, Task> WriteError)
		{
			if (Node.Credentials != null)
			{
				Dictionary<string, Credential> Cache = new Dictionary<string, Credential>(StringComparer.OrdinalIgnoreCase);
				foreach (KeyValuePair<string, string> Pair in Node.Credentials)
				{
					// Get the credential path. We expect this in the format <CredentialName>.<PropertyName>
					string Path = Pair.Value;

					// Find the separator
					int Idx = Path.IndexOf('.', StringComparison.Ordinal);
					if (Idx == -1)
					{
						await WriteError($"Invalid credential path '{Path}'. Requested credentials should be in the form '<CredentialName>.<PropertyName>'.\n");
						return false;
					}

					// Split the path into credential and property names
					string CredentialName = Path.Substring(0, Idx);
					string PropertyName = Path.Substring(Idx + 1);

					// Try to get the credential with this name
					Credential? Credential;
					if (!Cache.TryGetValue(CredentialName, out Credential))
					{
						Credential = await CredentialService.GetCredentialAsync(CredentialName);
						if (Credential == null)
						{
							await WriteError($"No credential called '{CredentialName}' could be found");
							return false;
						}
						if (!await CredentialService.AuthorizeAsync(Credential, AclAction.ViewCredential, User, null))
						{
							await WriteError($"User is not allowed to view credential '{CredentialName}'");
							return false;
						}
						Cache[CredentialName] = Credential;
					}

					// Get the property
					string? PropertyValue;
					if (!Credential!.Properties.TryGetValue(PropertyName, out PropertyValue))
					{
						await WriteError($"No property called '{PropertyName}' found in credential '{CredentialName}'");
						return false;
					}

					// Add it to the output list
					Credentials.Add(Pair.Key, PropertyValue);
				}
			}
			return true;
		}

		async Task<IJob> GetJobAsync(ObjectId JobId)
		{
			IJob? Job = await JobService.GetJobAsync(JobId);
			if (Job == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Job {JobId} not found", JobId);
			}
			return Job;
		}

		static IJobStepBatch AuthorizeBatch(IJob Job, SubResourceId BatchId, ServerCallContext Context)
		{
			IJobStepBatch? Batch;
			if (!Job.TryGetBatch(BatchId, out Batch))
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Unable to find batch {JobId}:{BatchId}", Job.Id, BatchId);
			}
			if (Batch.SessionId == null)
			{
				throw new StructuredRpcException(StatusCode.PermissionDenied, "Batch {JobId}:{BatchId} has no session id", Job.Id, BatchId);
			}

			ClaimsPrincipal Principal = Context.GetHttpContext().User;
			if (!Principal.HasSessionClaim(Batch.SessionId.Value))
			{
				throw new StructuredRpcException(StatusCode.PermissionDenied, "Session id {SessionId} not valid for batch {JobId}:{BatchId}. Expected {ExpectedSessionId}.", Principal.GetSessionClaim() ?? ObjectId.Empty, Job.Id, BatchId, Batch.SessionId.Value);
			}

			return Batch;
		}

		/// <summary>
		/// Updates the state of a jobstep
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<Empty> UpdateStep(UpdateStepRequest Request, ServerCallContext Context)
		{
			IJob? Job = await JobService.GetJobAsync(Request.JobId.ToObjectId());
			if (Job == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Job {JobId} not found", Request.JobId);
			}

			IJobStepBatch Batch = AuthorizeBatch(Job, Request.BatchId.ToSubResourceId(), Context);

			await JobService.UpdateStepAsync(Job, Batch.Id, Request.StepId.ToSubResourceId(), Request.State, Request.Outcome, null, null, null, null, null);
			return new Empty();
		}

		/// <summary>
		/// Get the state of a jobstep
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the step</returns>
		public async override Task<GetStepResponse> GetStep(GetStepRequest Request, ServerCallContext Context)
		{
			IJob Job = await GetJobAsync(Request.JobId.ToObjectId());
			IJobStepBatch Batch = AuthorizeBatch(Job, Request.BatchId.ToSubResourceId(), Context);

			SubResourceId StepId = Request.StepId.ToSubResourceId();
			if (!Batch.TryGetStep(StepId, out IJobStep? Step))
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Unable to find step {JobId}:{BatchId}:{StepId}", Job.Id, Batch.Id, StepId);
			}

			return new GetStepResponse(Step.Outcome, Step.State, Step.AbortRequested);
		}

		/// <summary>
		/// Updates the state of a jobstep
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<UpdateGraphResponse> UpdateGraph(UpdateGraphRequest Request, ServerCallContext Context)
		{
			List<Api.CreateGroupRequest> ApiGroups = new List<Api.CreateGroupRequest>();
			foreach (CreateGroupRequest Group in Request.Groups)
			{
				List<Api.CreateNodeRequest> ApiNodes = new List<Api.CreateNodeRequest>();
				foreach (CreateNodeRequest Node in Group.Nodes)
				{
					Api.CreateNodeRequest ApiNode = new Api.CreateNodeRequest(Node.Name, Node.InputDependencies.ToList(), Node.OrderDependencies.ToList(), Node.Priority, Node.AllowRetry, Node.RunEarly, Node.Warnings, new Dictionary<string, string>(Node.Credentials), new Dictionary<string, string>(Node.Properties));
					ApiNodes.Add(ApiNode);
				}
				ApiGroups.Add(new Api.CreateGroupRequest(Group.AgentType, ApiNodes));
			}

			List<Api.CreateAggregateRequest> ApiAggregates = new List<Api.CreateAggregateRequest>();
			foreach (CreateAggregateRequest Aggregate in Request.Aggregates)
			{
				Api.CreateAggregateRequest ApiAggregate = new Api.CreateAggregateRequest(Aggregate.Name, Aggregate.Nodes.ToList());
				ApiAggregates.Add(ApiAggregate);
			}

			List<Api.CreateLabelRequest> ApiLabels = new List<Api.CreateLabelRequest>();
			foreach (CreateLabelRequest Label in Request.Labels)
			{
				Api.CreateLabelRequest ApiLabel = new Api.CreateLabelRequest(Label.DashboardName, Label.DashboardCategory, Label.UgsName, Label.UgsProject, Label.Change, Label.RequiredNodes.ToList(), Label.IncludedNodes.ToList());
				ApiLabels.Add(ApiLabel);
			}

			ObjectId JobIdValue = Request.JobId.ToObjectId();
			for (; ; )
			{
				IJob? Job = await JobService.GetJobAsync(JobIdValue);
				if (Job == null)
				{
					throw new StructuredRpcException(StatusCode.NotFound, "Resource not found");
				}
				if (!JobService.AuthorizeSession(Job, Context.GetHttpContext().User))
				{
					throw new StructuredRpcException(StatusCode.PermissionDenied, "Access denied");
				}

				IGraph Graph = await JobService.GetGraphAsync(Job);
				Graph = await Graphs.AppendAsync(Graph, ApiGroups, ApiAggregates, ApiLabels);

				if (await JobService.TryUpdateGraphAsync(Job, Graph))
				{
					return new UpdateGraphResponse();
				}
			}
		}

		/// <summary>
		/// Creates a set of events
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<Empty> CreateEvents(CreateEventsRequest Request, ServerCallContext Context)
		{
			if (!await AclService.AuthorizeAsync(AclAction.CreateEvent, Context.GetHttpContext().User))
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Access denied");
			}

			List<NewLogEventData> NewEvents = new List<NewLogEventData>();
			foreach (CreateEventRequest Event in Request.Events)
			{
				NewLogEventData NewEvent = new NewLogEventData();
				NewEvent.LogId = Event.LogId.ToObjectId();
				NewEvent.Severity = Event.Severity;
				NewEvent.LineIndex = Event.LineIndex;
				NewEvent.LineCount = Event.LineCount;
				NewEvents.Add(NewEvent);
			}
			await LogFileService.CreateEventsAsync(NewEvents);
			return new Empty();
		}

		/// <summary>
		/// Writes output to a log file
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<Empty> WriteOutput(WriteOutputRequest Request, ServerCallContext Context)
		{
			ILogFile? LogFile = await LogFileService.GetCachedLogFileAsync(Request.LogId.ToObjectId());
			if (LogFile == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Resource not found");
			}
			if (!HordeServer.Services.LogFileService.AuthorizeForSession(LogFile, Context.GetHttpContext().User))
			{
				throw new StructuredRpcException(StatusCode.PermissionDenied, "Access denied");
			}

			await LogFileService.WriteLogDataAsync(LogFile, Request.Offset, Request.LineIndex, Request.Data.ToArray(), Request.Flush);
			return new Empty();
		}

		/// <summary>
		/// Uploads a new agent archive
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<UploadSoftwareResponse> UploadSoftware(UploadSoftwareRequest Request, ServerCallContext Context)
		{
			if (!await AclService.AuthorizeAsync(AclAction.UploadSoftware, Context.GetHttpContext().User))
			{
				throw new StructuredRpcException(StatusCode.PermissionDenied, "Access to software is forbidden");
			}

			AgentSoftwareVersion Version = await AgentSoftwareService.SetArchiveAsync(new AgentSoftwareChannelName(Request.Channel), null, Request.Data.ToArray());

			UploadSoftwareResponse Response = new UploadSoftwareResponse();
			Response.Version = Version.ToString();
			return Response;
		}

		/// <summary>
		/// Downloads a new agent archive
		/// </summary>
		/// <param name="Request">Request arguments</param>
		/// <param name="ResponseStream">Writer for the output data</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task DownloadSoftware(DownloadSoftwareRequest Request, IServerStreamWriter<DownloadSoftwareResponse> ResponseStream, ServerCallContext Context)
		{
			if (!await AclService.AuthorizeAsync(AclAction.DownloadSoftware, Context.GetHttpContext().User))
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Access to software is forbidden");
			}

			AgentSoftwareVersion Version = new AgentSoftwareVersion(Request.Version);

			byte[]? Data = await AgentSoftwareService.GetArchiveAsync(Version);
			if (Data == null)
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Missing version {Version}");
			}

			for (int Offset = 0; Offset < Data.Length;)
			{
				int NextOffset = Math.Min(Offset + 128 * 1024, Data.Length);

				DownloadSoftwareResponse Response = new DownloadSoftwareResponse();
				Response.Data = Google.Protobuf.ByteString.CopyFrom(Data.AsSpan(Offset, NextOffset - Offset));

				await ResponseStream.WriteAsync(Response);

				Offset = NextOffset;
			}
		}

		/// <summary>
		/// Uploads a new artifact
		/// </summary>
		/// <param name="Reader">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public async override Task<UploadArtifactResponse> UploadArtifact(IAsyncStreamReader<UploadArtifactRequest> Reader, ServerCallContext Context)
		{
			// Advance to the metadata object
			if (!await Reader.MoveNext())
			{
				throw new StructuredRpcException(StatusCode.DataLoss, "Missing request for artifact upload");
			}

			// Read the request object
			UploadArtifactMetadata? Metadata = Reader.Current.Metadata;
			if (Metadata == null)
			{
				throw new StructuredRpcException(StatusCode.DataLoss, "Expected metadata in first artifact request");
			}

			// Get the job and step
			IJob Job = await GetJobAsync(Metadata.JobId.ToObjectId());
			IJobStepBatch Batch = AuthorizeBatch(Job, Metadata.BatchId.ToSubResourceId(), Context);

			IJobStep? Step;
			if (!Job.TryGetStep(Metadata.BatchId.ToSubResourceId(), Metadata.StepId.ToSubResourceId(), out Step))
			{
				throw new StructuredRpcException(StatusCode.NotFound, "Unable to find step {JobId}:{BatchId}:{StepId}", Job.Id, Metadata.BatchId, Metadata.StepId);
			}

			// Upload the stream
			using (ArtifactChunkStream InputStream = new ArtifactChunkStream(Reader, Metadata.Length))
			{
				Artifact Artifact = await ArtifactService.CreateArtifactAsync(Job.Id, Step.Id, Metadata.Name, Metadata.MimeType, InputStream);

				UploadArtifactResponse Response = new UploadArtifactResponse();
				Response.Id = Artifact.Id.ToString();
				return Response;
			}
		}

		/// <summary>
		/// Uploads new test data
		/// </summary>
		/// <param name="Reader">Request arguments</param>
		/// <param name="Context">Context for the RPC call</param>
		/// <returns>Information about the new agent</returns>
		public override async Task<UploadTestDataResponse> UploadTestData(IAsyncStreamReader<UploadTestDataRequest> Reader, ServerCallContext Context)
		{
			IJob? Job = null;
			IJobStep? JobStep = null;

			while (await Reader.MoveNext())
			{
				UploadTestDataRequest Request = Reader.Current;

				ObjectId JobId = Request.JobId.ToObjectId();
				if (Job == null || JobId != Job.Id)
				{
					Job = await JobService.GetJobAsync(JobId);
					if (Job == null)
					{
						throw new StructuredRpcException(StatusCode.NotFound, "Unable to find job {JobId}", JobId);
					}
					JobStep = null;
				}

				SubResourceId JobStepId = Request.JobStepId.ToSubResourceId();
				if (JobStep == null || JobStepId != JobStep.Id)
				{
					if (!Job.TryGetStep(JobStepId, out JobStep))
					{
						throw new StructuredRpcException(StatusCode.NotFound, "Unable to find step {JobStepId} on job {JobId}", JobStepId, JobId);
					}
				}

				string Text = Encoding.UTF8.GetString(Request.Value.ToArray());
				BsonDocument Document = BsonSerializer.Deserialize<BsonDocument>(Text);
				await TestData.AddAsync(Job, JobStep, Request.Key, Document);
			}

			return new UploadTestDataResponse();
		}

		/// <summary>
		/// Create a new report on a job or job step
		/// </summary>
		/// <param name="Request"></param>
		/// <param name="Context"></param>
		/// <returns></returns>
		public override async Task<CreateReportResponse> CreateReport(CreateReportRequest Request, ServerCallContext Context)
		{
			IJob Job = await GetJobAsync(Request.JobId.ToObjectId());
			IJobStepBatch Batch = AuthorizeBatch(Job, Request.BatchId.ToSubResourceId(), Context);

			Report NewReport = new Report { Name = Request.Name, Placement = Request.Placement, ArtifactId = Request.ArtifactId.ToObjectId() };
			if (Request.Scope == ReportScope.Job)
			{
				Logger.LogDebug("Adding report to job {JobId}: {Name} -> {ArtifactId}", Job.Id, Request.Name, Request.ArtifactId);
				await JobService.UpdateJobAsync(Job, Reports: new List<Report> { NewReport });
			}
			else
			{
				Logger.LogDebug("Adding report to step {JobId}:{BatchId}:{StepId}: {Name} -> {ArtifactId}", Job.Id, Batch.Id, Request.StepId, Request.Name, Request.ArtifactId);
				await JobService.UpdateStepAsync(Job, Batch.Id, Request.StepId.ToSubResourceId(), NewReports: new List<Report> { NewReport });
			}

			return new CreateReportResponse();
		}
	}
}
