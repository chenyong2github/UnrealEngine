// Copyright Epic Games, Inc. All Rights Reserved.

using Datadog.Trace;
using EpicGames.Core;
using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using HordeCommon.Rpc.Tasks;
using HordeServer.Tasks;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Identity;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.IdentityModel.Tokens;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IdentityModel.Tokens.Jwt;
using System.Linq;
using System.Linq.Expressions;
using System.Security.Claims;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using StatsdClient;
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;
using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;
using AgentSoftwareChannelName = HordeServer.Utilities.StringId<HordeServer.Services.AgentSoftwareChannels>;
using System.Collections.Concurrent;
using Microsoft.Extensions.Caching.Memory;

namespace HordeServer.Services
{
	/// <summary>
	/// Wraps funtionality for manipulating agents
	/// </summary>
	public class AgentService : TickedBackgroundService
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
		IAgentCollection Agents;

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
		/// List of event listeners for agents connected to this server. Used to track leases that cannot be migrated to other machines (ie. REAPI leases)
		/// </summary>
		Dictionary<AgentId, AgentEventListener> Listeners = new Dictionary<AgentId, AgentEventListener>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Agents">Collection of agent documents</param>
		/// <param name="Leases">Collection of lease documents</param>
		/// <param name="Sessions">Collection of session documents</param>
		/// <param name="AclService">The ACL service</param>
		/// <param name="DowntimeService">The downtime service</param>
		/// <param name="DogStatsd">The DogStatsd metric client</param>
		/// <param name="TaskSources">The list of task sources</param>
		/// <param name="ApplicationLifetime"></param>
		/// <param name="Logger">Log output writer</param>
		/// <param name="Clock">Clock</param>
		public AgentService(IAgentCollection Agents, ILeaseCollection Leases, ISessionCollection Sessions, AclService AclService, IDowntimeService DowntimeService, IDogStatsd DogStatsd, IEnumerable<ITaskSource> TaskSources, IHostApplicationLifetime ApplicationLifetime, ILogger<AgentService> Logger, IClock Clock)
			: base(TimeSpan.FromSeconds(30.0), Logger)
		{
			this.Agents = Agents;
			this.Leases = Leases;
			this.Sessions = Sessions;
			this.AclService = AclService;
			this.DowntimeService = DowntimeService;
			this.DogStatsd = DogStatsd;
			this.TaskSources = TaskSources.ToArray();
			this.ApplicationLifetime = ApplicationLifetime;
			this.Logger = Logger;
			this.Clock = Clock;
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
		/// <param name="bEphemeral">Whether this agent is ephemeral</param>
		/// <param name="Channel">Override for the desired software version</param>
		/// <param name="Pools">Pools for this agent</param>
		/// <returns>Unique id for the agent</returns>
		public Task<IAgent> CreateAgentAsync(string Name, bool bEnabled, bool bEphemeral, AgentSoftwareChannelName? Channel, List<PoolId>? Pools)
		{
			return Agents.AddAsync(new AgentId(Name), bEnabled, bEphemeral, Channel, Pools);
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
		/// Update an agent's settings
		/// </summary>
		/// <param name="Agent">Agent instance</param>
		/// <param name="bNewEnabled">Whether the agent is enabled or not</param>
		/// <param name="bNewRequestConform">Whether to request a conform job be run</param>
		/// <param name="bNewRequestRestart">Whether to request the machine be restarted</param>
		/// <param name="bNewRequestShutdown">Whether to request the machine be shutdown</param>
		/// <param name="NewChannel">Override for the desired client version</param>
		/// <param name="NewPools">List of pools for the agent</param>
		/// <param name="NewAcl">New ACL for this agent</param>
		/// <param name="NewComment">New comment</param>
		/// <returns>Version of the software that needs to be installed on the agent. Null if the agent is running the correct version.</returns>
		public async Task<IAgent?> UpdateAgentAsync(IAgent? Agent, bool? bNewEnabled, bool? bNewRequestConform, bool? bNewRequestRestart, bool? bNewRequestShutdown, AgentSoftwareChannelName? NewChannel, List<PoolId>? NewPools, Acl? NewAcl, string? NewComment)
		{
			while (Agent != null)
			{
				// Apply the update
				IAgent? NewAgent = await Agents.TryUpdateSettingsAsync(Agent, bNewEnabled, bNewRequestConform, bNewRequestRestart, bNewRequestShutdown, NewChannel, NewPools, NewAcl, NewComment);
				if (NewAgent != null)
				{
					Agent = NewAgent;

					// If we're requesting a conform, allow the agent to break out of a long poll for work immediately to pick it up
//					if ((bNewRequestConform ?? false) || (bNewRequestRestart ?? false) || (bNewRequestShutdown ?? false))
//					{
//						DispatchService.CancelLongPollForAgent(Agent.Id);
//					}

					return Agent;
				}

				// Update the agent
				Agent = await GetAgentAsync(Agent.Id);
			}
			return null;
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

		/// <summary>
		/// Creates a new agent session
		/// </summary>
		/// <param name="Agent">The agent to create a session for</param>
		/// <param name="Status">Current status of the agent</param>
		/// <param name="Capabilities">Capabilities of the agent software</param>
		/// <param name="Version">Version of the software that's running</param>
		/// <returns>New agent state</returns>
		public async Task<IAgent> CreateSessionAsync(IAgent Agent, AgentStatus Status, AgentCapabilities Capabilities, AgentSoftwareVersion? Version)
		{
			for (; ; )
			{
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
						Logger.LogDebug("Removing outstanding lease {LeaseId} during session create...", Lease.Id);
						await RemoveLeaseAsync(Agent, Lease, UtcNow, LeaseOutcome.Failed);
					}

					// Create a new session document
					ISession NewSession = await Sessions.AddAsync(ObjectId.GenerateNewId(), Agent.Id, Clock.UtcNow, Capabilities, Version);
					DateTime SessionExpiresAt = UtcNow + SessionExpiryTime;

					// Reset the agent to use the new session
					NewAgent = await Agents.TryStartSessionAsync(Agent, NewSession.Id, SessionExpiresAt, Status, Capabilities, Version);
					if(NewAgent != null)
					{
						Agent = NewAgent;
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

		/// <summary>
		/// Waits for a lease to be assigned to an agent
		/// </summary>
		/// <param name="Agent">The agent to assign a lease to</param>
		/// <param name="AbortTask">Task which can be used to stop polling</param>
		/// <returns>True if a lease was assigned, false otherwise</returns>
		public async Task<IAgent?> WaitForLeaseAsync(IAgent? Agent, Task AbortTask)
		{
			while (Agent != null)
			{
				// Check there's an active session
				if (!Agent.SessionExpiresAt.HasValue)
				{
					break;
				}

				// Check we have some time to wait
				DateTime UtcNow = Clock.UtcNow;
				TimeSpan MaxWaitTime = (Agent.SessionExpiresAt.Value - SessionExpiryTime + SessionLongPollTime) - UtcNow;
				if(MaxWaitTime < TimeSpan.Zero)
				{
					break;
				}

				// If we're in a maintenance window, just wait for the time to expire
				if (DowntimeService.IsDowntimeActive)
				{
					await Task.WhenAny(AbortTask, Task.Delay(MaxWaitTime));
					break;
				}

				// Subscribe for any new tasks becoming available
				List<ITaskListener> Subscriptions = new List<ITaskListener>();
				try
				{
					Task WaitTask = Task.Delay(MaxWaitTime, ApplicationLifetime.ApplicationStopping);

					// Create a list of tasks to wait for
					List<Task> Tasks = new List<Task>();
					Tasks.Add(WaitTask);
					Tasks.Add(AbortTask);

					// Try to create a waiter for each task source
					foreach (ITaskSource TaskSource in TaskSources)
					{
						ITaskListener? NewSubscription = await TaskSource.SubscribeAsync(Agent);
						if (NewSubscription != null)
						{
							Subscriptions.Add(NewSubscription);
							if (!NewSubscription.LeaseTask.IsCompleted || NewSubscription.LeaseTask.Result != null)
							{
								Tasks.Add(NewSubscription.LeaseTask);
							}
							if (NewSubscription.LeaseTask.IsCompleted)
							{
								break;
							}
						}
					}

					// Wait for any lease to complete
					Task? Result = Tasks.FirstOrDefault(x => x.IsCompleted);
					if (Result == null)
					{
						try
						{
							Logger.LogDebug("Start long poll for agent {AgentId}", Agent.Id);
							Result = await Task.WhenAny(Tasks);
							Logger.LogDebug("Long poll for agent {AgentId} stopped by {Idx}", Agent.Id, Tasks.IndexOf(Result));
						}
						catch (OperationCanceledException)
						{
							Logger.LogDebug("Stopped long poll (application stopping)");
							break;
						}
					}

					// Log the outcome of the log poll
					if (Result == WaitTask)
					{
						Logger.LogDebug("Stopped long poll (timeout)");
						break;
					}
					else if (Result == AbortTask)
					{
						Logger.LogDebug("Stopped long poll (client abort)");
						break;
					}

					// If we got some new work, try to add a lease for it
					Logger.LogDebug("Stopped long poll (found work)");

					ITaskListener? Subscription = Subscriptions.FirstOrDefault(x => x.LeaseTask.IsCompleted);
					if (Subscription != null)
					{
						NewLeaseInfo? NewAgentLease = await Subscription.LeaseTask;
						if (NewAgentLease != null)
						{
							IAgent? NewAgent = await Agents.TryAddLeaseAsync(Agent, NewAgentLease.Lease);
							if(NewAgent != null)
							{
								Subscription.Accepted = true;
								if (NewAgentLease.OnConnectionLost != null)
								{
									AddListener(Agent.Id, NewAgentLease.Lease.Id, NewAgentLease.OnConnectionLost);
								}
								await CreateLeaseAsync(Agent, NewAgentLease.Lease);
								return NewAgent;
							}
						}
					}
				}
				finally
				{
					foreach (ITaskListener Waiter in Subscriptions)
					{
						await Waiter.DisposeAsync();
					}
				}

				// Update the agent
				Agent = await GetAgentAsync(Agent.Id);
			}
			return Agent;
		}

		/// <summary>
		/// Updates the state of the current agent session
		/// </summary>
		/// <param name="InAgent">The current agent state</param>
		/// <param name="SessionId">Id of the session</param>
		/// <param name="Status">New status for the agent</param>
		/// <param name="Capabilities">New agent capabilities</param>
		/// <param name="NewLeases">New list of leases for this session</param>
		/// <returns>Updated agent state</returns>
		public async Task<IAgent?> UpdateSessionAsync(IAgent InAgent, ObjectId SessionId, AgentStatus Status, AgentCapabilities? Capabilities, IList<HordeCommon.Rpc.Messages.Lease> NewLeases)
		{
			AgentEventListener? Listener = null;
			try
			{
				lock (Listeners)
				{
					if (Listeners.TryGetValue(InAgent.Id, out Listener))
					{
						Listener.BeginUpdate(InAgent.Leases.Select(x => x.Id));
					}
				}

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
					Dictionary<ObjectId, HordeCommon.Rpc.Messages.Lease> LeaseIdToNewState = NewLeases.ToDictionary(x => x.Id.ToObjectId(), x => x);
					for (int Idx = 0; Idx < Leases.Count; Idx++)
					{
						AgentLease Lease = Leases[Idx];
						if (Lease.State == LeaseState.Cancelled)
						{
							HordeCommon.Rpc.Messages.Lease? NewLease;
							if (!LeaseIdToNewState.TryGetValue(Lease.Id, out NewLease) || NewLease.State == LeaseState.Cancelled || NewLease.State == LeaseState.Completed)
							{
								await RemoveLeaseAsync(Agent, Lease, UtcNow, LeaseOutcome.Cancelled);
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
									await RemoveLeaseAsync(Agent, Lease, UtcNow, NewLease.Outcome);
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

					// Update the agent, and try to create new lease documents if we succeed
					IAgent? NewAgent = await Agents.TryUpdateSessionAsync(Agent, Status, SessionExpiresAt, Capabilities, bUpdateLeases ? Leases : null);
					if (NewAgent != null)
					{
						break;
					}

					// Fetch the agent again
					Agent = await GetAgentAsync(Agent.Id);
				}
				return Agent;
			}
			finally
			{
				Listener?.EndUpdate();
			}
		}

		void AddListener(AgentId AgentId, ObjectId LeaseId, Action Callback)
		{
			lock (Listeners)
			{
				AgentEventListener? Listener;
				if (!Listeners.TryGetValue(AgentId, out Listener) || !Listener.TryAdd(LeaseId, Callback))
				{
					Listener = new AgentEventListener(this, AgentId, LeaseId, Callback);
					Listeners[AgentId] = Listener;
				}
			}
		}

		void RemoveListener(AgentEventListener Listener)
		{
			lock (Listeners)
			{
				AgentEventListener? CurrentListener;
				if (Listeners.TryGetValue(Listener.AgentId, out CurrentListener) && CurrentListener == Listener)
				{
					Listeners.Remove(Listener.AgentId);
				}
			}
			Listener.Dispose();
		}

		class AgentEventListener : IDisposable
		{
			object LockObject = new object();
			AgentService AgentService;
			public AgentId AgentId;
			Timer? Timer;
			List<(ObjectId, Action)> LeaseCallbacks = new List<(ObjectId, Action)>();

			public AgentEventListener(AgentService AgentService, AgentId AgentId, ObjectId LeaseId, Action Callback)
			{
				this.AgentService = AgentService;
				this.AgentId = AgentId;

				Timer = new Timer(x => AgentService.RemoveListener(this));
				LeaseCallbacks.Add((LeaseId, Callback));
			}

			public void Dispose()
			{
				if (Timer != null)
				{
					BeginUpdate(Array.Empty<ObjectId>());
					Timer.Dispose();
					Timer = null;
				}
			}

			public bool TryAdd(ObjectId LeaseId, Action Callback)
			{
				lock (LockObject)
				{
					if (LeaseCallbacks.Count > 0)
					{
						LeaseCallbacks.Add((LeaseId, Callback));
						return true;
					}
				}
				return false;
			}

			public void BeginUpdate(IEnumerable<ObjectId> LeaseIds)
			{
				lock (LockObject)
				{
					if (LeaseCallbacks.Count > 0)
					{
						// Execute any callbacks for leases that have completed
						for (int Idx = LeaseCallbacks.Count - 1; Idx >= 0; Idx--)
						{
							if (!LeaseIds.Contains(LeaseCallbacks[Idx].Item1))
							{
								LeaseCallbacks[Idx].Item2();
								LeaseCallbacks.RemoveAt(Idx);
							}
						}

						// Return false when we don't have any remaining callbacks
						if (LeaseCallbacks.Count == 0)
						{
							AgentService.RemoveListener(this);
						}
						else
						{
							Timer?.Change(Timeout.Infinite, Timeout.Infinite);
						}
					}
				}
			}

			public void EndUpdate()
			{
				lock (LockObject)
				{
					Timer?.Change(20 * 1000, Timeout.Infinite);
				}
			}
		}

		class AgentUpdateScope : IDisposable
		{
			AgentEventListener Listener;

			public AgentUpdateScope(AgentEventListener Listener)
			{
				this.Listener = Listener;
			}

			public void Dispose()
			{
				Listener.EndUpdate();
			}
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
					Logger.LogDebug("Removing lease {LeaseId} during session terminate...", Lease.Id);
					await RemoveLeaseAsync(Agent, Lease, FinishTime, LeaseOutcome.Failed);
				}

				// Update the session document
				await Sessions.UpdateAsync(SessionId, FinishTime, Agent.Capabilities);
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
			return Leases.AddAsync(AgentLease.Id, AgentLease.Name, Agent.Id, Agent.SessionId!.Value, AgentLease.StreamId, AgentLease.PoolId, AgentLease.LogId, AgentLease.StartTime, AgentLease.Payload!);
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
		public Task<ILease?> GetLeaseAsync(ObjectId LeaseId)
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
		/// <returns>Async task</returns>
		private async Task RemoveLeaseAsync(IAgent Agent, AgentLease Lease, DateTime UtcNow, LeaseOutcome Outcome)
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
						await TaskSource.AbortTaskAsync(Agent, Lease.Id, Any);
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
			await Leases.TrySetOutcomeAsync(Lease.Id, FinishTime, Outcome);
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
