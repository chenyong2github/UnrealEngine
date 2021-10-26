// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Interface for a collection of lease documents
	/// </summary>
	public interface ILeaseCollection
	{
		/// <summary>
		/// Adds a lease to the collection
		/// </summary>
		/// <param name="Id">The lease id</param>
		/// <param name="Name">Name of the lease</param>
		/// <param name="AgentId">The agent id</param>
		/// <param name="SessionId">The agent session handling the lease</param>
		/// <param name="StreamId">Stream for the payload</param>
		/// <param name="PoolId">The pool for the lease</param>
		/// <param name="LogId">Log id for the lease</param>
		/// <param name="StartTime">Start time of the lease</param>
		/// <param name="Payload">Payload for the lease</param>
		/// <returns>Async task</returns>
		Task<ILease> AddAsync(LeaseId Id, string Name, AgentId AgentId, ObjectId SessionId, StreamId? StreamId, PoolId? PoolId, LogId? LogId, DateTime StartTime, byte[] Payload);

		/// <summary>
		/// Deletes a lease from the collection
		/// </summary>
		/// <param name="LeaseId">Unique id of the lease</param>
		/// <returns>Async task</returns>
		Task DeleteAsync(LeaseId LeaseId);

		/// <summary>
		/// Gets a specific lease
		/// </summary>
		/// <param name="LeaseId">Unique id of the lease</param>
		/// <returns>The lease that was found, or null if it does not exist</returns>
		Task<ILease?> GetAsync(LeaseId LeaseId);

		/// <summary>
		/// Finds all leases matching a set of criteria
		/// </summary>
		/// <param name="AgentId">Unqiue id of the agent executing this lease</param>
		/// <param name="SessionId">Unique id of the agent session</param>
		/// <param name="MinTime">Start of the window to include leases</param>
		/// <param name="MaxTime">End of the window to include leases</param>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of leases matching the given criteria</returns>
		Task<List<ILease>> FindLeasesAsync(AgentId? AgentId = null, ObjectId? SessionId = null, DateTime? MinTime = null, DateTime? MaxTime = null, int? Index = null, int? Count = null);

		/// <summary>
		/// Finds all active leases
		/// </summary>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of leases</returns>
		Task<List<ILease>> FindActiveLeasesAsync(int? Index = null, int? Count = null);

		/// <summary>
		/// Sets the outcome of a lease
		/// </summary>
		/// <param name="LeaseId">The lease to update</param>
		/// <param name="FinishTime">Time at which the lease finished</param>
		/// <param name="Outcome">Outcome of the lease</param>
		/// <param name="Output">Output data from the task</param>
		/// <returns>True if the lease was updated, false otherwise</returns>
		Task<bool> TrySetOutcomeAsync(LeaseId LeaseId, DateTime FinishTime, LeaseOutcome Outcome, byte[]? Output);
	}
}
