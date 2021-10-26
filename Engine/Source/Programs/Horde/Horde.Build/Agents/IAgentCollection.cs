// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Models;
using HordeCommon.Rpc.Tasks;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using PoolId = StringId<IPool>;
	using AgentSoftwareChannelName = StringId<AgentSoftwareChannels>;

	/// <summary>
	/// Interface for a collection of agent documents
	/// </summary>
	public interface IAgentCollection
	{
		/// <summary>
		/// Adds a new agent with the given properties
		/// </summary>
		/// <param name="Id">Id for the new agent</param>
		/// <param name="bEnabled">Whether the agent is enabled or not</param>
		/// <param name="Channel">Channel to use for software run by this agent</param>
		/// <param name="Pools">Pools for the agent</param>
		Task<IAgent> AddAsync(AgentId Id, bool bEnabled, AgentSoftwareChannelName? Channel = null, List<PoolId>? Pools = null);

		/// <summary>
		/// Deletes an agent
		/// </summary>
		/// <param name="Agent">Deletes the agent</param>
		/// <returns>Async task</returns>
		Task<IAgent?> TryDeleteAsync(IAgent Agent);

		/// <summary>
		/// Deletes an agent
		/// </summary>
		/// <param name="AgentId">Unique id of the agent</param>
		/// <returns>Async task</returns>
		Task ForceDeleteAsync(AgentId AgentId);

		/// <summary>
		/// Gets an agent by ID
		/// </summary>
		/// <param name="AgentId">Unique id of the agent</param>
		/// <returns>The agent document</returns>
		Task<IAgent?> GetAsync(AgentId AgentId);

		/// <summary>
		/// Finds all agents matching certain criteria
		/// </summary>
		/// <param name="Pool">The pool containing the agent</param>
		/// <param name="PoolId">The pool ID in string form containing the agent</param>
		/// <param name="ModifiedAfter">If set, only returns agents modified after this time</param>
		/// <param name="Status">Status to look for</param>
		/// <param name="Index">Index of the first result</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of agents matching the given criteria</returns>
		Task<List<IAgent>> FindAsync(ObjectId? Pool = null, string? PoolId = null, DateTime? ModifiedAfter = null, AgentStatus? Status = null, int? Index = null, int? Count = null);

		/// <summary>
		/// Finds all the expried agents
		/// </summary>
		/// <param name="UtcNow">The current time</param>
		/// <param name="MaxAgents">Maximum number of agents to return</param>
		/// <returns>List of agents</returns>
		Task<List<IAgent>> FindExpiredAsync(DateTime UtcNow, int MaxAgents);

		/// <summary>
		/// Update an agent's settings
		/// </summary>
		/// <param name="Agent">Agent instance</param>
		/// <param name="bEnabled">Whether the agent is enabled or not</param>
		/// <param name="bRequestConform">Whether to request a conform job be run</param>
		/// <param name="bRequestRestart">Whether to request the machine be restarted</param>
		/// <param name="bRequestShutdown">Whether to request the machine be shut down</param>
		/// <param name="Channel">Override for the desired software channel</param>
		/// <param name="Pools">List of pools for the agent</param>
		/// <param name="Acl">New ACL for this agent</param>
		/// <param name="Comment">New comment</param>
		/// <returns>Version of the software that needs to be installed on the agent. Null if the agent is running the correct version.</returns>
		Task<IAgent?> TryUpdateSettingsAsync(IAgent Agent, bool? bEnabled = null, bool? bRequestConform = null, bool? bRequestRestart = null, bool? bRequestShutdown = null, AgentSoftwareChannelName? Channel = null, List<PoolId>? Pools = null, Acl? Acl = null, string? Comment = null);

		/// <summary>
		/// Update the current workspaces for an agent.
		/// </summary>
		/// <param name="Agent">The agent to update</param>
		/// <param name="Workspaces">Current list of workspaces</param>
		/// <param name="RequestConform">Whether the agent still needs to run another conform</param>
		/// <returns>New agent state</returns>
		Task<IAgent?> TryUpdateWorkspacesAsync(IAgent Agent, List<AgentWorkspace> Workspaces, bool RequestConform);

		/// <summary>
		/// Sets the current session
		/// </summary>
		/// <param name="Agent">The agent to update</param>
		/// <param name="SessionId">New session id</param>
		/// <param name="SessionExpiresAt">Expiry time for the new session</param>
		/// <param name="Status">Status of the agent</param>
		/// <param name="Properties">Properties for the current session</param>
		/// <param name="Resources">Resources for the agent</param>
		/// <param name="DynamicPools">New list of dynamic pools for the agent</param>
		/// <param name="Version">Current version of the agent software</param>
		/// <returns>New agent state</returns>
		Task<IAgent?> TryStartSessionAsync(IAgent Agent, ObjectId SessionId, DateTime SessionExpiresAt, AgentStatus Status, IReadOnlyList<string> Properties, IReadOnlyDictionary<string, int> Resources, IReadOnlyList<PoolId> DynamicPools, string? Version);

		/// <summary>
		/// Attempt to update the agent state
		/// </summary>
		/// <param name="Agent">Agent instance</param>
		/// <param name="Status">New status of the agent</param>
		/// <param name="SessionExpiresAt">New expiry time for the current session</param>
		/// <param name="Properties">Properties for the current session</param>
		/// <param name="Resources">Resources for the agent</param>
		/// <param name="DynamicPools">New list of dynamic pools for the agent</param>
		/// <param name="Leases">New set of leases</param>
		/// <returns>True if the document was updated, false if another writer updated the document first</returns>
		Task<IAgent?> TryUpdateSessionAsync(IAgent Agent, AgentStatus? Status, DateTime? SessionExpiresAt, IReadOnlyList<string>? Properties, IReadOnlyDictionary<string, int>? Resources, IReadOnlyList<PoolId>? DynamicPools, List<AgentLease>? Leases);

		/// <summary>
		/// Terminates the current session
		/// </summary>
		/// <param name="Agent">The agent to terminate the session for</param>
		/// <returns>New agent state if it succeeded, otherwise null</returns>
		Task<IAgent?> TryTerminateSessionAsync(IAgent Agent);

		/// <summary>
		/// Attempts to add a lease to an agent
		/// </summary>
		/// <param name="Agent">The agent to update</param>
		/// <param name="NewLease">The new lease document</param>
		/// <returns>New agent state if it succeeded, otherwise null</returns>
		Task<IAgent?> TryAddLeaseAsync(IAgent Agent, AgentLease NewLease);

		/// <summary>
		/// Attempts to cancel a lease
		/// </summary>
		/// <param name="Agent">The agent to update</param>
		/// <param name="LeaseIdx">Index of the lease</param>
		/// <returns>New agent state if it succeeded, otherwise null</returns>
		Task<IAgent?> TryCancelLeaseAsync(IAgent Agent, int LeaseIdx);

		/// <summary>
		/// Gets the log channel for an agent
		/// </summary>
		/// <param name="AgentId"></param>
		/// <returns></returns>
		IAuditLogChannel<AgentId> GetLogger(AgentId AgentId);

		/// <summary>
		/// Subscribe to notifications on agent states being updated
		/// </summary>
		/// <param name="OnUpdate">Callback for updates</param>
		/// <returns>Disposable subscription object</returns>
		Task<IDisposable> SubscribeToUpdateEventsAsync(Action<AgentId> OnUpdate);
	}
}
