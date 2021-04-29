// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;

namespace HordeServer.Services
{
	/// <summary>
	/// Wraps functionality for manipulating pools
	/// </summary>
	public class PoolService
	{
		/// <summary>
		/// Collection of pool documents
		/// </summary>
		IPoolCollection Pools;

		/// <summary>
		/// Returns the current time
		/// </summary>
		IClock Clock;

		/// <summary>
		/// Cached set of pools, along with the timestamp that it was obtained
		/// </summary>
		Tuple<DateTime, Dictionary<PoolId, IPool>>? CachedPoolLookup;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Pools">Collection of pool documents</param>
		/// <param name="Clock"></param>
		public PoolService(IPoolCollection Pools, IClock Clock)
		{
			this.Pools = Pools;
			this.Clock = Clock;
		}

		/// <summary>
		/// Creates a new pool
		/// </summary>
		/// <param name="Name">Name of the new pool</param>
		/// <param name="Requirements">Requirements for agents to add to this pool</param>
		/// <param name="EnableAutoscaling">Whether to enable autoscaling for this pool</param>
		/// <param name="MinAgents">Minimum number of agents in the pool</param>
		/// <param name="NumReserveAgents">Minimum number of idle agents to maintain</param>
		/// <param name="Properties">Properties for the new pool</param>
		/// <returns>The new pool document</returns>
		public Task<IPool> CreatePoolAsync(string Name, AgentRequirements? Requirements = null, bool? EnableAutoscaling = null, int? MinAgents = null, int? NumReserveAgents = null, Dictionary<string, string>? Properties = null)
		{
			return Pools.AddAsync(PoolId.Sanitize(Name), Name, Requirements, EnableAutoscaling, MinAgents, NumReserveAgents, Properties);
		}

		/// <summary>
		/// Deletes a pool
		/// </summary>
		/// <param name="PoolId">Unique id of the pool</param>
		/// <returns>Async task object</returns>
		public Task<bool> DeletePoolAsync(PoolId PoolId)
		{
			return Pools.DeleteAsync(PoolId);
		}

		/// <summary>
		/// Updates an existing pool
		/// </summary>
		/// <param name="Pool">The pool to update</param>
		/// <param name="NewName">The new name for the pool</param>
		/// <param name="NewRequirements">New requirements for the pool</param>
		/// <param name="NewEnableAutoscaling">Whether to enable autoscaling</param>
		/// <param name="NewMinAgents">Minimum number of agents in the pool</param>
		/// <param name="NewNumReserveAgents">Minimum number of idle agents to maintain</param>
		/// <param name="NewProperties">Properties on the pool to update. Any properties with a value of null will be removed.</param>
		/// <returns>Async task object</returns>
		public async Task<IPool?> UpdatePoolAsync(IPool? Pool, string? NewName = null, AgentRequirements? NewRequirements = null, bool? NewEnableAutoscaling = null, int? NewMinAgents = null, int? NewNumReserveAgents = null, Dictionary<string, string?>? NewProperties = null)
		{
			for (; Pool != null; Pool = await Pools.GetAsync(Pool.Id))
			{
				IPool? NewPool = await Pools.TryUpdateAsync(Pool, NewName, NewRequirements, NewEnableAutoscaling, NewMinAgents, NewNumReserveAgents, null, NewProperties, null, null);
				if (NewPool != null)
				{
					return NewPool;
				}
			}
			return Pool;
		}

		/// <summary>
		/// Gets all the available pools
		/// </summary>
		/// <returns>List of pool documents</returns>
		public Task<List<IPool>> GetPoolsAsync()
		{
			return Pools.GetAsync();
		}

		/// <summary>
		/// Gets a pool by ID
		/// </summary>
		/// <param name="PoolId">Unique id of the pool</param>
		/// <returns>The pool document</returns>
		public Task<IPool?> GetPoolAsync(PoolId PoolId)
		{
			return Pools.GetAsync(PoolId);
		}

		/// <summary>
		/// Get a list of workspaces for the given agent
		/// </summary>
		/// <param name="Agent">The agent to return workspaces for</param>
		/// <param name="ValidAtTime">Absolute time at which we expect the results to be valid. Values may be cached as long as they are after this time.</param>
		/// <returns>List of workspaces</returns>
		public async Task<HashSet<AgentWorkspace>> GetWorkspacesAsync(IAgent Agent, DateTime ValidAtTime)
		{
			HashSet<AgentWorkspace> Workspaces = new HashSet<AgentWorkspace>();
			if (Agent.Capabilities.Devices.Count > 0)
			{
				DeviceCapabilities PrimaryDevice = Agent.Capabilities.Devices[0];
				if (PrimaryDevice.Properties != null)
				{
					if (PrimaryDevice.Properties.Contains("OSFamily=Windows"))
					{
						Workspaces.Add(new AgentWorkspace(null, null, AgentWorkspace.AutoSdkIdentifier, "//UE4/Private-AutoSDK-Windows", null, true));
					}
					else if (PrimaryDevice.Properties.Contains("OSVersion=MacOS"))
					{
						Workspaces.Add(new AgentWorkspace(null, null, AgentWorkspace.AutoSdkIdentifier, "//UE4/Private-AutoSDK-Mac", null, true));
					}
				}
			}

			Dictionary<PoolId, IPool> PoolMapping = await GetPoolLookupAsync(ValidAtTime);
			Workspaces.UnionWith(Agent.GetPools(PoolMapping.Values).SelectMany(x => x.Workspaces));
			return Workspaces;
		}

		/// <summary>
		/// Gets a mapping from pool identifiers to definitions
		/// </summary>
		/// <param name="ValidAtTime">Absolute time at which we expect the results to be valid. Values may be cached as long as they are after this time.</param>
		/// <returns>Map of pool ids to pool documents</returns>
		private async Task<Dictionary<PoolId, IPool>> GetPoolLookupAsync(DateTime ValidAtTime)
		{
			Tuple<DateTime, Dictionary<PoolId, IPool>>? CachedPoolLookupCopy = CachedPoolLookup;
			if (CachedPoolLookupCopy == null || CachedPoolLookupCopy.Item1 < ValidAtTime)
			{
				// Get a new list of cached pools
				DateTime NewCacheTime = Clock.UtcNow;
				List<IPool> NewPools = await Pools.GetAsync();
				Tuple<DateTime, Dictionary<PoolId, IPool>> NewCachedPoolLookup = Tuple.Create(NewCacheTime, NewPools.ToDictionary(x => x.Id, x => x));

				// Try to swap it with the current version
				while (CachedPoolLookupCopy == null || CachedPoolLookupCopy.Item1 < NewCacheTime)
				{
					Tuple<DateTime, Dictionary<PoolId, IPool>>? OriginalValue = Interlocked.CompareExchange(ref CachedPoolLookup, NewCachedPoolLookup, CachedPoolLookupCopy);
					if (OriginalValue == CachedPoolLookupCopy)
					{
						CachedPoolLookupCopy = NewCachedPoolLookup;
						break;
					}
					CachedPoolLookupCopy = OriginalValue;
				}
			}
			return CachedPoolLookupCopy.Item2;
		}
	}
}
