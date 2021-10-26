// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Tasks;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Driver;
using StatsdClient;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Security.Claims;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	using AgentSoftwareChannelName = StringId<AgentSoftwareChannels>;
	using LeaseId = ObjectId<ILease>;
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Wraps funtionality for manipulating agents
	/// </summary>
	public sealed class AgentService : TickedBackgroundService
	{
		/// <summary>
		/// Maximum time between updates for an agent to be considered online
		/// </summary>
		public static readonly TimeSpan SessionExpiryTime = TimeSpan.FromMinutes(5);

		/// <summary>
		/// Time before a session expires that we will poll until
		/// </summary>
		public static readonly TimeSpan SessionLongPollTime = TimeSpan.FromSeconds(55);

		/// <summary>
		/// Time after which a session will be renewed
		/// </summary>
		public static readonly TimeSpan SessionRenewTime = TimeSpan.FromSeconds(50);

		/// <summary>
		/// The ACL service instance
		/// </summary>
		AclService AclService;

		/// <summary>
		/// The downtime service instance
		/// </summary>
		IDowntimeService DowntimeService;

		/// <summary>
		/// Collection of agent documents
		/// </summary>
		public IAgentCollection Agents { get; }

		/// <summary>
		/// Collection of lease documents
		/// </summary>
		ILeaseCollection Leases;

		/// <summary>
		/// Collection of session documents
		/// </summary>
		ISessionCollection Sessions;

		/// <summary>
		/// DogStatsD metric client
		/// </summary>
		IDogStatsd DogStatsd;

		/// <summary>
		/// List of task sources
		/// </summary>
		ITaskSource[] TaskSources;

		/// <summary>
		/// The application lifetime instance
		/// </summary>
		IHostApplicationLifetime ApplicationLifetime;

		/// <summary>
		/// Log output writer
		/// </summary>
		ILogger<AgentService> Logger;
		
		/// <summary>
		/// Clock
		/// </summary>
		readonly IClock Clock;

		/// <summary>
		/// Lazily updated list of current pools
		/// </summary>
		AsyncCachedValue<List<IPool>> PoolsList;

		/// <summary>
		/// All the agents currently performing a long poll for work on this server
		/// </summary>
		Dictionary<AgentId, CancellationTokenSource> WaitingAgents = new Dictionary<AgentId, CancellationTokenSource>();

		/// <summary>
		/// Subscription for update events
		/// </summary>
		IDisposable Subscription;

		/// <summary>
		/// Constructor
		/// </summary>
		public AgentService(IAgentCollection Agents, ILeaseCollection Leases, ISessionCollection Sessions, AclService AclService, IDowntimeService DowntimeService, IPoolCollection PoolCollection, IDogStatsd DogStatsd, IEnumerable<ITaskSource> TaskSources, IHostApplicationLifetime ApplicationLifetime, ILogger<AgentService> Logger, IClock Clock)
			: base(TimeSpan.FromSeconds(30.0), Logger)
		{
			this.Agents = Agents;
			this.Leases = Leases;
			this.Sessions = Sessions;
			this.AclService = AclService;
			this.DowntimeService = DowntimeService;
			this.PoolsList = new AsyncCachedValue<List<IPool>>(() => PoolCollection.GetAsync(), TimeSpan.FromSeconds(30.0));
			this.DogStatsd = DogStatsd;
			this.TaskSources = TaskSources.ToArray();
			this.ApplicationLifetime = ApplicationLifetime;
			this.Logger = Logger;
			this.Clock = Clock;

			Subscription = Agents.SubscribeToUpdateEventsAsync(OnAgentUpdate).Result;
		}

		/// <inheritdoc/>
		public override void Dispose()
		{
			base.Dispose();
			Subscription.Dispose();
		}

		/// <summary>
		/// Issues a bearer token for the given session id
		/// </summary>
		/// <param name="SessionId">The session id</param>
		/// <returns>Bearer token for the agent</returns>
		public string IssueSessionToken(ObjectId SessionId)
		{
			List<AclClaim> Claims = new List<AclClaim>();
			Claims.Add(AclService.AgentClaim);
			Claims.Add(AclService.GetSessionClaim(SessionId));
			return AclService.IssueBearerToken(Claims, null);
		}

		/// <summary>
		/// Register a new agent
		/// </summary>
		/// <param name="Name">Name of the agent</param>
		/// <param name="bEnabled">Whether the agent is currently enabled</param>
		/// <param name="Channel">Override for the desired software version</param>
		/// <param name="Pools">Pools for this agent</param>
		/// <returns>Unique id for the agent</returns>
		public Task<IAgent> CreateAgentAsync(string Name, bool bEnabled, AgentSoftwareChannelName? Channel, List<PoolId>? Pools)
		{
			return Agents.AddAsync(new AgentId(Name), bEnabled, Channel, Pools);
		}

		/// <summary>
		/// Gets an agent by ID
		/// </summary>
		/// <param name="AgentId">Unique id of the agent</param>
		/// <returns>The agent document</returns>
		public Task<IAgent?> GetAgentAsync(AgentId AgentId)
		{
			return Agents.GetAsync(AgentId);
		}

		/// <summary>
		/// Finds all agents matching certain criteria
		/// </summary>
		/// <param name="Pool">The pool containing the agent</param>
		/// <param name="ModifiedAfter">If set, only returns agents modified after this time</param>
		/// <param name="Index">Index within the list of results</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of agents matching the given criteria</returns>
		public Task<List<IAgent>> FindAgentsAsync(ObjectId? Pool, DateTime? ModifiedAfter, int? Index, int? Count)
		{
			return Agents.FindAsync(Pool, null, ModifiedAfter, null, Index, Count);
		}

		/// <summary>
		/// Update the current workspaces for an agent.
		/// </summary>
		/// <param name="Agent">The agent to update</param>
		/// <param name="Workspaces">Current list of workspaces</param>
		/// <param name="bPendingConform">Whether the agent still needs to run another conform</param>
		/// <returns>New agent state</returns>
		public async Task<bool> TryUpdateWorkspacesAsync(IAgent Agent, List<AgentWorkspace> Workspaces, bool bPendingConform)
		{
			IAgent? NewAgent = await Agents.TryUpdateWorkspacesAsync(Agent, Workspaces, bPendingConform);
			return NewAgent != null;
		}

		/// <summary>
		/// Marks the agent as deleted
		/// </summary>
		/// <param name="Agent">The agent to delete</param>
		/// <returns>Async task</returns>
		public async Task DeleteAgentAsync(IAgent? Agent)
		{
			while (Agent != null && !Agent.Deleted)
			{
				IAgent? NewAgent = await Agents.TryDeleteAsync(Agent);
				if(NewAgent != null)
				{
					break;
				}
				Agent = await GetAgentAsync(Agent.Id);
			}
		}

		async ValueTask<List<PoolId>> GetDynamicPoolsAsync(IAgent Agent)
		{
			List<PoolId> NewDynamicPools = new List<PoolId>();

			List<IPool> Pools = await PoolsList.GetAsync();
			foreach (IPool Pool in Pools)
			{
				if (Pool.Condition != null && Agent.SatisfiesCondition(Pool.Condition))
				{
					NewDynamicPools.Add(Pool.Id);
				}
			}

			return NewDynamicPools;
		}

		/// <summary>
		/// Callback for an agents 
		/// </summary>
		/// <param name="AgentId"></param>
		void OnAgentUpdate(AgentId AgentId)
		{
			lock (WaitingAgents)
			{
				if (WaitingAgents.TryGetValue(AgentId, out CancellationTokenSource? CancellationSource))
				{
					CancellationSource.Cancel();
				}
			}
		}

		/// <summary>
		/// Creates a new agent session
		/// </summary>
		/// <param name="Agent">The agent to create a session for</param>
		/// <param name="Status">Current status of the agent</param>
		/// <param name="Properties">Properties for the agent</param>
		/// <param name="Resources">Resources which the agent has</param>
		/// <param name="Version">Version of the software that's running</param>
		/// <returns>New agent state</returns>
		public async Task<IAgent> CreateSessionAsync(IAgent Agent, AgentStatus Status, IReadOnlyList<string> Properties, IReadOnlyDictionary<string, int> Resources, string? Version)
		{
			for (; ; )
			{
				IAuditLogChannel<AgentId> AgentLogger = Agents.GetLogger(Agent.Id);

				// Check if there's already a session running for this agent.
				IAgent? NewAgent;
				if (Agent.SessionId != null)
				{
					// Try to terminate the current session
					await TryTerminateSessionAsync(Agent);
				}
				else
				{
					DateTime UtcNow = Clock.UtcNow;

					// Remove any outstanding leases
					foreach (AgentLease Lease in Agent.Leases)
					{
						AgentLogger.LogInformation("Removing outstanding lease {LeaseId}", Lease.Id);
						await RemoveLeaseAsync(Agent, Lease, UtcNow, LeaseOutcome.Failed, null);
					}

					// Create a new session document
					ISession NewSession = await Sessions.AddAsync(ObjectId.GenerateNewId(), Agent.Id, Clock.UtcNow, Properties, Resources, Version);
					DateTime SessionExpiresAt = UtcNow + SessionExpiryTime;

					// Get the new dynamic pools for the agent
					List<PoolId> DynamicPools = await GetDynamicPoolsAsync(Agent);

					// Reset the agent to use the new session
					NewAgent = await Agents.TryStartSessionAsync(Agent, NewSession.Id, SessionExpiresAt, Status, Properties, Resources, DynamicPools, Version);
					if(NewAgent != null)
					{
						Agent = NewAgent;
						AgentLogger.LogInformation("Session {SessionId} started", NewSession.Id);
						break;
					}

					// Remove the session we didn't use
					await Sessions.DeleteAsync(NewSession.Id);
				}

				// Get the current agent state
				NewAgent = await GetAgentAsync(Agent.Id);
				if (NewAgent == null)
				{
					throw new InvalidOperationException($"Invalid agent id '{Agent.Id}'");
				}
				Agent = NewAgent;
			}
			return Agent;
		}

		async Task<(ITaskSource, AgentLease)?> GuardedWaitForLeaseAsync(ITaskSource Source, IAgent Agent, CancellationToken CancellationToken)
		{
			try
			{
				AgentLease? Lease = await Source.AssignLeaseAsync(Agent, CancellationToken);
				return (Lease != null) ? (Source, Lease) : null;
			}
			catch (TaskCanceledException)
			{
				return null;
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Exception while trying to assign lease"); 
				return null;
			}
		}

		/// <summary>
		/// Waits for a lease to be assigned to an agent
		/// </summary>
		/// <param name="Agent">The agent to assign a lease to</param>
		/// <param name="CancellationToken"></param>
		/// <returns>True if a lease was assigned, false otherwise</returns>
		public async Task<IAgent?> WaitForLeaseAsync(IAgent? Agent, CancellationToken CancellationToken)
		{
			while (Agent != null)
			{
				if (!Agent.SessionExpiresAt.HasValue)
				{
					break;
				}

				// Check we have some time to wait
				DateTime UtcNow = Clock.UtcNow;
				TimeSpan MaxWaitTime = (Agent.SessionExpiresAt.Value - SessionExpiryTime + SessionLongPollTime) - UtcNow;
				if (MaxWaitTime <= TimeSpan.Zero)
				{
					break;
				}

				// Create a cancellation token that will expire with the session
				using CancellationTokenSource CancellationSource = CancellationTokenSource.CreateLinkedTokenSource(ApplicationLifetime.ApplicationStopping, CancellationToken);
				CancellationSource.CancelAfter(MaxWaitTime);

				// Assign a new lease
				(ITaskSource, AgentLease)? Result = null;
				try
				{
					// Add the cancellation source to the set of waiting agents
					lock (WaitingAgents)
					{
						WaitingAgents[Agent.Id] = CancellationSource;
					}

					// If we're in a maintenance window, just wait for the time to expire
					if (DowntimeService.IsDowntimeActive)
					{
						await AsyncUtils.DelayNoThrow(MaxWaitTime, CancellationToken);
						break;
					}

					// Create all the tasks to wait for
					List<Task<(ITaskSource, AgentLease)?>> Tasks = new List<Task<(ITaskSource, AgentLease)?>>();
					foreach (ITaskSource TaskSource in TaskSources)
					{
						Tasks.Add(GuardedWaitForLeaseAsync(TaskSource, Agent, CancellationSource.Token));
					}

					// Wait for a lease to be available
					while (Tasks.Count > 0)
					{
						await Task.WhenAny(Tasks);

						for (int Idx = 0; Idx < Tasks.Count; Idx++)
						{
							Task<(ITaskSource, AgentLease)?> Task = Tasks[Idx];
							if (!Task.IsCompleted)
							{
								continue;
							}

							Tasks.RemoveAt(Idx--);

							(ITaskSource, AgentLease)? TaskResult = Task.Result;
							if (!TaskResult.HasValue)
							{
								continue;
							}

							if (Result == null)
							{
								Result = TaskResult;
								CancellationSource.Cancel();
							}
							else
							{
								(ITaskSource TaskSource, AgentLease TaskLease) = TaskResult.Value;
								await TaskSource.CancelLeaseAsync(Agent, TaskLease.Id, Any.Parser.ParseFrom(TaskLease.Payload));
							}
						}
					}
				}
				finally
				{
					lock (WaitingAgents)
					{
						WaitingAgents.Remove(Agent.Id);
					}
				}

				// Exit if we didn't find any work to do. It may be that all the task sources returned null, in which case wait for the time period to expire.
				if (Result == null)
				{
					if (!CancellationSource.IsCancellationRequested)
					{
						await CancellationSource.Token.AsTask();
					}
					break;
				}

				// Get the resulting lease
				(ITaskSource Source, AgentLease Lease) = Result.Value;
				if (Lease == AgentLease.Drain)
				{
					await AsyncUtils.DelayNoThrow(MaxWaitTime, CancellationToken);
					break;
				}

				// Add the new lease to the agent
				IAgent? NewAgent = await Agents.TryAddLeaseAsync(Agent, Lease);
				if (NewAgent != null)
				{
					await Source.OnLeaseStartedAsync(NewAgent, Lease.Id, Any.Parser.ParseFrom(Lease.Payload), Agents.GetLogger(Agent.Id));
					await CreateLeaseAsync(Agent, Lease);
					return NewAgent;
				}

				// Update the agent
				Agent = await GetAgentAsync(Agent.Id);
			}
			return Agent;
		}

		/// <summary>
		/// Cancels the specified agent lease
		/// </summary>
		/// <param name="Agent">Agent to cancel the lease on</param>
		/// <param name="LeaseId">The lease id to cancel</param>
		/// <returns></returns>
		public async Task<bool> CancelLeaseAsync(IAgent Agent, LeaseId LeaseId)
		{		
			int Index = 0;
			while (Index < Agent.Leases.Count && Agent.Leases[Index].Id != LeaseId)
			{
				Index++;
			}

			if (Index == Agent.Leases.Count)
			{
				return false;
			}

			if (Agent.Leases[Index].State == LeaseState.Cancelled)
			{
				return false;
			}

			await Agents.TryCancelLeaseAsync(Agent, Index);
			return true;
		}

		/// <summary>
		/// 
		/// </summary>
		public async Task<IAgent?> UpdateSessionWithWaitAsync(IAgent InAgent, ObjectId SessionId, AgentStatus Status, IReadOnlyList<string>? Properties, IReadOnlyDictionary<string, int>? Resources, IList<HordeCommon.Rpc.Messages.Lease> NewLeases, CancellationToken CancellationToken)
		{
			IAgent? Agent = InAgent;

			// Capture the current agent update index. This allows us to detect if anything has changed.
			uint UpdateIndex = Agent.UpdateIndex;

			// Update the agent session and return to the caller if anything changes
			Agent = await UpdateSessionAsync(Agent, SessionId, Status, Properties, Resources, NewLeases);
			if (Agent != null && Agent.UpdateIndex == UpdateIndex && (Agent.Leases.Count > 0 || Agent.Status != AgentStatus.Stopping))
			{
				Agent = await WaitForLeaseAsync(Agent, CancellationToken);
			}
			return Agent;
		}

		/// <summary>
		/// Updates the state of the current agent session
		/// </summary>
		/// <param name="InAgent">The current agent state</param>
		/// <param name="SessionId">Id of the session</param>
		/// <param name="Status">New status for the agent</param>
		/// <param name="Properties">New agent properties</param>
		/// <param name="Resources">New agent resources</param>
		/// <param name="NewLeases">New list of leases for this session</param>
		/// <returns>Updated agent state</returns>
		public async Task<IAgent?> UpdateSessionAsync(IAgent InAgent, ObjectId SessionId, AgentStatus Status, IReadOnlyList<string>? Properties, IReadOnlyDictionary<string, int>? Resources, IList<HordeCommon.Rpc.Messages.Lease> NewLeases)
		{
			DateTime UtcNow = Clock.UtcNow;

			Stopwatch Timer = Stopwatch.StartNew();

			IAgent? Agent = InAgent;
			while (Agent != null)
			{
				// If the agent is stopping and doesn't have any leases, we can terminate the current session.
				if (Status == AgentStatus.Stopping && NewLeases.Count == 0)
				{
					// If we've already decided to terminate the session, this update is redundant but harmless
					if (Agent.SessionId != SessionId)
					{
						break;
					}

					// If the session is valid, we can terminate once the agent leases are also empty
					if (Agent.Leases.Count == 0)
					{
						if (!await TryTerminateSessionAsync(Agent))
						{
							Agent = await GetAgentAsync(Agent.Id);
							continue;
						}
						break;
					}
				}

				// Check the session id is correct.
				if (Agent.SessionId != SessionId)
				{
					throw new InvalidOperationException($"Invalid agent session {SessionId}");
				}

				// Check the session hasn't expired
				if (!Agent.IsSessionValid(UtcNow))
				{
					throw new InvalidOperationException("Session has already expired");
				}

				// Extend the current session time if we're within a time period of the current time expiring. This reduces
				// unnecessary DB writes a little, but it also allows us to skip the update and jump into a long poll state
				// if there's still time on the session left.
				DateTime? SessionExpiresAt = null;
				if (!Agent.SessionExpiresAt.HasValue || UtcNow > (Agent.SessionExpiresAt - SessionExpiryTime) + SessionRenewTime)
				{
					SessionExpiresAt = UtcNow + SessionExpiryTime;
				}

				// Flag for whether the leases array should be updated
				bool bUpdateLeases = false;
				List<AgentLease> Leases = new List<AgentLease>(Agent.Leases);

				// Remove any completed leases from the agent
				Dictionary<LeaseId, HordeCommon.Rpc.Messages.Lease> LeaseIdToNewState = NewLeases.ToDictionary(x => new LeaseId(x.Id), x => x);
				for (int Idx = 0; Idx < Leases.Count; Idx++)
				{
					AgentLease Lease = Leases[Idx];
					if (Lease.State == LeaseState.Cancelled)
					{
						HordeCommon.Rpc.Messages.Lease? NewLease;
						if (!LeaseIdToNewState.TryGetValue(Lease.Id, out NewLease) || NewLease.State == LeaseState.Cancelled || NewLease.State == LeaseState.Completed)
						{
							await RemoveLeaseAsync(Agent, Lease, UtcNow, LeaseOutcome.Cancelled, null);
							Leases.RemoveAt(Idx--);
							bUpdateLeases = true;
						}
					}
					else
					{
						HordeCommon.Rpc.Messages.Lease? NewLease;
						if (LeaseIdToNewState.TryGetValue(Lease.Id, out NewLease) && NewLease.State != Lease.State)
						{
							if (NewLease.State == LeaseState.Cancelled || NewLease.State == LeaseState.Completed)
							{
								await RemoveLeaseAsync(Agent, Lease, UtcNow, NewLease.Outcome, NewLease.Output.ToByteArray());
								Leases.RemoveAt(Idx--);
							}
							else if (NewLease.State == LeaseState.Active && Lease.State == LeaseState.Pending)
							{
								Lease.State = LeaseState.Active;
							}
							bUpdateLeases = true;
						}
					}
				}

				// If the agent is stopping, cancel all the leases. Clear out the current session once it's complete.
				if (Status == AgentStatus.Stopping)
				{
					foreach (AgentLease Lease in Leases)
					{
						if (Lease.State != LeaseState.Cancelled)
						{
							Lease.State = LeaseState.Cancelled;
							bUpdateLeases = true;
						}
					}
				}

				// Get the new dynamic pools for the agent
				List<PoolId> DynamicPools = await GetDynamicPoolsAsync(Agent);

				// Update the agent, and try to create new lease documents if we succeed
				IAgent? NewAgent = await Agents.TryUpdateSessionAsync(Agent, Status, SessionExpiresAt, Properties, Resources, DynamicPools, bUpdateLeases ? Leases : null);
				if (NewAgent != null)
				{
					Agent = NewAgent;
					break;
				}

				// Fetch the agent again
				Agent = await GetAgentAsync(Agent.Id);
			}
			return Agent;
		}

		/// <summary>
		/// Terminates an existing session. Does not update the agent itself, if it's currently 
		/// </summary>
		/// <param name="Agent">The agent whose current session should be terminated</param>
		/// <returns>True if the session was terminated</returns>
		private async Task<bool> TryTerminateSessionAsync(IAgent Agent)
		{
			// Make sure the agent has a valid session id
			if (Agent.SessionId == null)
			{
				return true;
			}

			// Get the time that the session finishes at
			DateTime FinishTime = Clock.UtcNow;
			if (Agent.SessionExpiresAt.HasValue && Agent.SessionExpiresAt.Value < FinishTime)
			{
				FinishTime = Agent.SessionExpiresAt.Value;
			}

			// Save off the session id and current leases
			ObjectId SessionId = Agent.SessionId.Value;
			List<AgentLease> Leases = new List<AgentLease>(Agent.Leases);

			// Clear the current session
			IAgent? NewAgent = await Agents.TryTerminateSessionAsync(Agent);
			if (NewAgent != null)
			{
				Agent = NewAgent;

				// Remove any outstanding leases
				foreach(AgentLease Lease in Leases)
				{
					Agents.GetLogger(Agent.Id).LogInformation("Removing lease {LeaseId} during session terminate...", Lease.Id);
					await RemoveLeaseAsync(Agent, Lease, FinishTime, LeaseOutcome.Failed, null);
				}

				// Update the session document
				Agents.GetLogger(Agent.Id).LogInformation("Terminated session {SessionId}", SessionId);
				await Sessions.UpdateAsync(SessionId, FinishTime, Agent.Properties, Agent.Resources);
				return true;
			}
			return false;
		}

		/// <summary>
		/// Creates a new lease document
		/// </summary>
		/// <param name="Agent">Agent that will be executing the lease</param>
		/// <param name="AgentLease">The new agent lease</param>
		/// <returns>New lease document</returns>
		private Task<ILease> CreateLeaseAsync(IAgent Agent, AgentLease AgentLease)
		{
			try
			{
				return Leases.AddAsync(AgentLease.Id, AgentLease.Name, Agent.Id, Agent.SessionId!.Value, AgentLease.StreamId, AgentLease.PoolId, AgentLease.LogId, AgentLease.StartTime, AgentLease.Payload!);
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Unable to create lease {LeaseId} for agent {AgentId}; lease already exists?", AgentLease.Id, Agent.Id);
				throw;
			}
		}

		/// <summary>
		/// Finds all leases matching a set of criteria
		/// </summary>
		/// <param name="AgentId">Unqiue id of the agent executing this lease</param>
		/// <param name="SessionId">Unique id of the agent session</param>
		/// <param name="StartTime">Start of the search window to return results for</param>
		/// <param name="FinishTime">End of the search window to return results for</param>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of leases matching the given criteria</returns>
		public Task<List<ILease>> FindLeasesAsync(AgentId? AgentId, ObjectId? SessionId, DateTime? StartTime, DateTime? FinishTime, int Index, int Count)
		{
			return Leases.FindLeasesAsync(AgentId, SessionId, StartTime, FinishTime, Index, Count);
		}

		/// <summary>
		/// Gets a specific lease
		/// </summary>
		/// <param name="LeaseId">Unique id of the lease</param>
		/// <returns>The lease that was found, or null if it does not exist</returns>
		public Task<ILease?> GetLeaseAsync(LeaseId LeaseId)
		{
			return Leases.GetAsync(LeaseId);
		}

		/// <summary>
		/// Removes a lease with the given id. Updates the lease state in the database, and removes the item from the agent's leases array.
		/// </summary>
		/// <param name="Agent">The agent to remove a lease from</param>
		/// <param name="Lease">The lease to cancel</param>
		/// <param name="UtcNow">The current time</param>
		/// <param name="Outcome">Final status of the lease</param>
		/// <param name="Output">Output from executing the task</param>
		/// <returns>Async task</returns>
		private async Task RemoveLeaseAsync(IAgent Agent, AgentLease Lease, DateTime UtcNow, LeaseOutcome Outcome, byte[]? Output)
		{
			// Make sure the lease is terminated correctly
			if (Lease.Payload == null)
			{
				Logger.LogWarning("Removing lease {LeaseId} (no payload)", Lease.Id);
			}
			else
			{
				Any Any = Any.Parser.ParseFrom(Lease.Payload);
				Logger.LogInformation("Removing lease {LeaseId} ({LeaseType})", Lease.Id, Any.TypeUrl);

				foreach (ITaskSource TaskSource in TaskSources)
				{
					if (Any.Is(TaskSource.Descriptor))
					{
						await TaskSource.OnLeaseFinishedAsync(Agent, Lease.Id, Any, Outcome, Output, Agents.GetLogger(Agent.Id));
						break;
					}
				}
			}

			// Figure out what time the lease finished
			DateTime FinishTime = UtcNow;
			if (Agent.SessionExpiresAt.HasValue && Agent.SessionExpiresAt.Value < FinishTime)
			{
				FinishTime = Agent.SessionExpiresAt.Value;
			}
			if (Lease.ExpiryTime.HasValue && Lease.ExpiryTime.Value < FinishTime)
			{
				FinishTime = Lease.ExpiryTime.Value;
			}

			// Update the lease
			await Leases.TrySetOutcomeAsync(Lease.Id, FinishTime, Outcome, Output);
		}

		/// <summary>
		/// Gets information about a particular session
		/// </summary>
		/// <param name="SessionId">The unique session id</param>
		/// <returns>The session information</returns>
		public Task<ISession?> GetSessionAsync(ObjectId SessionId)
		{
			return Sessions.GetAsync(SessionId);
		}

		/// <summary>
		/// Find sessions for the given agent
		/// </summary>
		/// <param name="AgentId">The unique agent id</param>
		/// <param name="StartTime">Start time to include in the search</param>
		/// <param name="FinishTime">Finish time to include in the search</param>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of sessions matching the given criteria</returns>
		public Task<List<ISession>> FindSessionsAsync(AgentId AgentId, DateTime? StartTime, DateTime? FinishTime, int Index, int Count)
		{
			return Sessions.FindAsync(AgentId, StartTime, FinishTime, Index, Count);
		}

		/// <summary>
		/// Terminate any sessions for agents that are offline
		/// </summary>
		/// <param name="StoppingToken">Token indicating the service is shutting down</param>
		/// <returns>Async task</returns>
		protected override async Task TickAsync(CancellationToken StoppingToken)
		{
			while (!StoppingToken.IsCancellationRequested)
			{
				// Find all the agents which are ready to be expired
				const int MaxAgents = 100;
				DateTime UtcNow = Clock.UtcNow;
				List<IAgent> ExpiredAgents = await Agents.FindExpiredAsync(UtcNow, MaxAgents);

				// Transition each agent to being offline
				foreach (IAgent ExpiredAgent in ExpiredAgents)
				{
					StoppingToken.ThrowIfCancellationRequested();
					Logger.LogDebug("Terminating session {SessionId} for agent {Agent}", ExpiredAgent.SessionId, ExpiredAgent.Id);
					await TryTerminateSessionAsync(ExpiredAgent);
				}

				// Try again if we didn't fetch everything
				if(ExpiredAgents.Count < MaxAgents)
				{
					break;
				}
			}

			await CollectMetrics();
		}

		private async Task CollectMetrics()
		{
			List<IAgent> AgentList = await Agents.FindAsync();
			int NumAgentsTotal = AgentList.Count;
			int NumAgentsTotalEnabled = AgentList.Count(a => a.Enabled);
			int NumAgentsTotalDisabled = AgentList.Count(a => !a.Enabled);
			int NumAgentsTotalOk = AgentList.Count(a => a.Enabled && a.Status == AgentStatus.Ok);
			int NumAgentsTotalStopping = AgentList.Count(a => a.Enabled && a.Status == AgentStatus.Stopping);
			int NumAgentsTotalUnhealthy = AgentList.Count(a => a.Enabled && a.Status == AgentStatus.Unhealthy);
			int NumAgentsTotalUnspecified = AgentList.Count(a => a.Enabled && a.Status == AgentStatus.Unspecified);
			
			// TODO: utilize tags argument in a smarter way below
			DogStatsd.Gauge("agents.total.count", NumAgentsTotal);
			DogStatsd.Gauge("agents.total.enabled.count", NumAgentsTotalEnabled);
			DogStatsd.Gauge("agents.total.disabled.count", NumAgentsTotalDisabled);
			DogStatsd.Gauge("agents.total.ok.count", NumAgentsTotalOk);
			DogStatsd.Gauge("agents.total.stopping.count", NumAgentsTotalStopping);
			DogStatsd.Gauge("agents.total.unhealthy.count", NumAgentsTotalUnhealthy);
			DogStatsd.Gauge("agents.total.unspecified.count", NumAgentsTotalUnspecified);
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular agent
		/// </summary>
		/// <param name="Agent">The agent to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">The permissions cache</param>
		/// <returns>True if the action is authorized</returns>
		public async Task<bool> AuthorizeAsync(IAgent Agent, AclAction Action, ClaimsPrincipal User, GlobalPermissionsCache? Cache)
		{
			bool? Result = Agent.Acl?.Authorize(Action, User);
			if (Result == null)
			{
				return await AclService.AuthorizeAsync(Action, User, Cache);
			}
			else
			{
				return Result.Value;
			}
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular agent
		/// </summary>
		/// <param name="Agent">The agent to check</param>
		/// <param name="User">The principal to authorize</param>
		/// <returns>True if the action is authorized</returns>
		public bool AuthorizeSession(IAgent Agent, ClaimsPrincipal User)
		{
			if (Agent.SessionId != null && User.HasSessionClaim(Agent.SessionId.Value) && Agent.IsSessionValid(Clock.UtcNow))
			{
				return true;
			}
			else
			{
				return false;
			}
		}
	}
}
