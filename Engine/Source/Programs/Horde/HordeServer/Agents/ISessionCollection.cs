// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.DependencyInjection;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	/// <summary>
	/// Interface for a collection of session documents
	/// </summary>
	public interface ISessionCollection
	{
		/// <summary>
		/// Adds a new session to the collection
		/// </summary>
		/// <param name="Id">The session id</param>
		/// <param name="AgentId">The agent this session is for</param>
		/// <param name="StartTime">Start time of this session</param>
		/// <param name="Properties">Properties of this agent at the time the session started</param>
		/// <param name="Resources">Resources which the agent has</param>
		/// <param name="Version">Version of the agent software</param>
		Task<ISession> AddAsync(ObjectId Id, AgentId AgentId, DateTime StartTime, IReadOnlyList<string>? Properties, IReadOnlyDictionary<string, int>? Resources, string? Version);

		/// <summary>
		/// Gets information about a particular session
		/// </summary>
		/// <param name="SessionId">The unique session id</param>
		/// <returns>The session information</returns>
		Task<ISession?> GetAsync(ObjectId SessionId);

		/// <summary>
		/// Find sessions for the given agent
		/// </summary>
		/// <param name="AgentId">The unique agent id</param>
		/// <param name="StartTime">Start time to include in the search</param>
		/// <param name="FinishTime">Finish time to include in the search</param>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of sessions matching the given criteria</returns>
		Task<List<ISession>> FindAsync(AgentId AgentId, DateTime? StartTime, DateTime? FinishTime, int Index, int Count);

		/// <summary>
		/// Finds any active sessions
		/// </summary>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of sessions</returns>
		Task<List<ISession>> FindActiveSessionsAsync(int? Index = null, int? Count = null);

		/// <summary>
		/// Update a session from the collection
		/// </summary>
		/// <param name="SessionId">The session to update</param>
		/// <param name="FinishTime">Time at which the session finished</param>
		/// <param name="Properties">The agent properties</param>
		/// <param name="Resources">Resources which the agent has</param>
		/// <returns>Async task</returns>
		Task UpdateAsync(ObjectId SessionId, DateTime FinishTime, IReadOnlyList<string> Properties, IReadOnlyDictionary<string, int> Resources);

		/// <summary>
		/// Delete a session from the collection
		/// </summary>
		/// <param name="SessionId">The session id</param>
		/// <returns>Async task</returns>
		Task DeleteAsync(ObjectId SessionId);
	}
}
