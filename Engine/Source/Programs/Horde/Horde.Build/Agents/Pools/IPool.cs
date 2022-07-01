// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using EpicGames.Horde.Common;
using Horde.Build.Agents.Fleet;
using Horde.Build.Utilities;

namespace Horde.Build.Agents.Pools
{
	using PoolId = StringId<IPool>;

	/// <summary>
	/// A pool of machines
	/// </summary>
	public interface IPool
	{
		/// <summary>
		/// Unique id for this pool
		/// </summary>
		public PoolId Id { get; }

		/// <summary>
		/// Name of the pool
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Condition for agents to automatically be included in this pool
		/// </summary>
		public Condition? Condition { get; }

		/// <summary>
		/// List of workspaces currently assigned to this pool
		/// </summary>
		public IReadOnlyList<AgentWorkspace> Workspaces { get; }

		/// <summary>
		/// Whether to sync autosdk alongside the workspaces for this pool
		/// </summary>
		public bool UseAutoSdk { get; }

		/// <summary>
		/// Arbitrary properties related to this pool
		/// </summary>
		public IReadOnlyDictionary<string, string> Properties { get; }

		/// <summary>
		/// Whether to enable autoscaling for this pool
		/// </summary>
		public bool EnableAutoscaling { get; }

		/// <summary>
		/// The minimum number of agents to keep in the pool
		/// </summary>
		public int? MinAgents { get; }

		/// <summary>
		/// The minimum number of idle agents to hold in reserve
		/// </summary>
		public int? NumReserveAgents { get; }

		/// <summary>
		/// Interval between conforms. If zero, the pool will not conform on a schedule.
		/// </summary>
		public TimeSpan? ConformInterval { get; }

		/// <summary>
		/// Last time the pool was (auto) scaled up
		/// </summary>
		public DateTime? LastScaleUpTime { get; }
		
		/// <summary>
		/// Last time the pool was (auto) scaled down
		/// </summary>
		public DateTime? LastScaleDownTime { get; }
		
		/// <summary>
		/// Cooldown time between scale-out events
		/// </summary>
		public TimeSpan? ScaleOutCooldown { get; }
		
		/// <summary>
		/// Cooldown time between scale-in events
		/// </summary>
		public TimeSpan? ScaleInCooldown { get; }
		
		/// <summary>
		/// Pool sizing strategy to be used for this pool
		/// </summary>
		public PoolSizeStrategy SizeStrategy { get; }
		
		/// <summary>
		/// Settings for lease utilization pool sizing strategy (if used)
		/// </summary>
		public LeaseUtilizationSettings? LeaseUtilizationSettings { get; }
		
		/// <summary>
		/// Settings for job queue pool sizing strategy (if used)
		/// </summary>
		public JobQueueSettings? JobQueueSettings { get; }

		/// <summary>
		/// Update index for this document
		/// </summary>
		public int UpdateIndex { get; }
	}
}
