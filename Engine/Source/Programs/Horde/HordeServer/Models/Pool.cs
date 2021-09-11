// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Common;
using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Permissions;
using System.Threading.Tasks;

namespace HordeServer.Models
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
		/// Last time the pool was (auto) scaled up
		/// </summary>
		public DateTime? LastScaleUpTime { get; }
		
		/// <summary>
		/// Last time the pool was (auto) scaled down
		/// </summary>
		public DateTime? LastScaleDownTime { get; }

		/// <summary>
		/// Update index for this document
		/// </summary>
		public int UpdateIndex { get; }
	}
}
