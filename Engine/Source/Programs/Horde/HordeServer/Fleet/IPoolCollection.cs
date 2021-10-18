// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Common;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
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

	/// <summary>
	/// Collection of pool documents
	/// </summary>
	public interface IPoolCollection
	{
		/// <summary>
		/// Creates a new pool
		/// </summary>
		/// <param name="Id">Unique id for the new pool</param>
		/// <param name="Name">Name of the new pool</param>
		/// <param name="Condition">Condition for agents to be included in this pool</param>
		/// <param name="EnableAutoscaling">Whether to enable autoscaling for this pool</param>
		/// <param name="MinAgents">Minimum number of agents in the pool</param>
		/// <param name="NumReserveAgents">Minimum number of idle agents to maintain</param>
		/// <param name="Properties">Properties for the pool</param>
		/// <returns>The new pool document</returns>
		Task<IPool> AddAsync(PoolId Id, string Name, Condition? Condition = null, bool? EnableAutoscaling = null, int? MinAgents = null, int? NumReserveAgents = null, IEnumerable<KeyValuePair<string, string>>? Properties = null);

		/// <summary>
		/// Enumerates all the pools
		/// </summary>
		/// <returns>The pool documents</returns>
		Task<List<IPool>> GetAsync();

		/// <summary>
		/// Gets a pool by ID
		/// </summary>
		/// <param name="Id">Unique id of the pool</param>
		/// <returns>The pool document</returns>
		Task<IPool?> GetAsync(PoolId Id);

		/// <summary>
		/// Gets a list of all valid pool ids
		/// </summary>
		/// <returns>List of pool ids</returns>
		Task<List<PoolId>> GetPoolIdsAsync();

		/// <summary>
		/// Gets a pool by ID
		/// </summary>
		/// <param name="Id">Unique id of the pool</param>
		/// <returns>The pool document</returns>
		Task<bool> DeleteAsync(PoolId Id);

		/// <summary>
		/// Updates a pool
		/// </summary>
		/// <param name="Pool">The pool to update</param>
		/// <param name="NewName">New name for the pool</param>
		/// <param name="NewCondition">New condition for the pool</param>
		/// <param name="NewEnableAutoscaling">New setting for whether to enable autoscaling</param>
		/// <param name="NewMinAgents">Minimum number of agents in the pool</param>
		/// <param name="NewNumReserveAgents">Minimum number of idle agents to maintain</param>
		/// <param name="NewWorkspaces">New workspaces for the pool</param>
		/// <param name="NewProperties">New properties for the pool</param>
		/// <param name="LastScaleUpTime">New time for last (auto) scale up</param>
		/// <param name="LastScaleDownTime">New time for last (auto) scale down</param>
		/// <returns>Async task</returns>
		Task<IPool?> TryUpdateAsync(IPool Pool, string? NewName = null, Condition? NewCondition = null, bool? NewEnableAutoscaling = null, int? NewMinAgents = null, int? NewNumReserveAgents = null, List<AgentWorkspace>? NewWorkspaces = null, Dictionary<string, string?>? NewProperties = null, DateTime? LastScaleUpTime = null, DateTime? LastScaleDownTime = null);
	}
}
