// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Common;
using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using Horde.Build.Fleet.Autoscale;

namespace HordeServer.Api
{
	/// <see cref="Horde.Build.Fleet.Autoscale.LeaseUtilizationSettings" />
	public class LeaseUtilizationSettings
	{
		/// <summary>
		/// Construct a public REST API representation from the internal one
		/// </summary>
		/// <param name="Settings"></param>
		public LeaseUtilizationSettings(Horde.Build.Fleet.Autoscale.LeaseUtilizationSettings Settings)
		{
		}
		
		/// <summary>
		/// Convert this public REST API object to an internal representation
		/// </summary>
		/// <returns></returns>
		public Horde.Build.Fleet.Autoscale.LeaseUtilizationSettings Convert()
		{
			return new Horde.Build.Fleet.Autoscale.LeaseUtilizationSettings();
		}
	}
	
	/// <see cref="Horde.Build.Fleet.Autoscale.JobQueueSettings" />
	public class JobQueueSettings
	{
		/// <summary>
		/// Factor translating queue size to additional agents to grow the pool with
		/// The result is always rounded up to nearest integer. 
		/// Example: if there are 20 jobs in queue, a factor 0.25 will result in 5 new agents being added (20 * 0.25)
		/// </summary>
		public double ScaleOutFactor { get; set; }

		/// <summary>
		/// Factor by which to shrink the pool size with when queue is empty
		/// The result is always rounded up to nearest integer.
		/// Example: when the queue size is zero, a default value of 0.9 will shrink the pool by 10% (current agent count * 0.9)
		/// </summary>
		public double ScaleInFactor { get; set; }

		/// <summary>
		/// Construct a public REST API representation from the internal one
		/// </summary>
		/// <param name="Settings"></param>
		public JobQueueSettings(Horde.Build.Fleet.Autoscale.JobQueueSettings Settings)
		{
			ScaleInFactor = Settings.ScaleInFactor;
			ScaleOutFactor = Settings.ScaleOutFactor;
		}
		
		/// <summary>
		/// Convert this public REST API object to an internal representation
		/// </summary>
		/// <returns></returns>
		public Horde.Build.Fleet.Autoscale.JobQueueSettings Convert()
		{
			return new Horde.Build.Fleet.Autoscale.JobQueueSettings(ScaleOutFactor, ScaleInFactor);
		}
	}
	
	/// <summary>
	/// Parameters to create a new pool
	/// </summary>
	public class CreatePoolRequest
	{
		/// <summary>
		/// Name for the new pool
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// Condition to satisfy for agents to be included in this pool
		/// </summary>
		public Condition? Condition { get; set; }

		/// <summary>
		/// Whether to enable autoscaling for this pool
		/// </summary>
		public bool? EnableAutoscaling { get; set; }
		
		/// <summary>
		/// Cooldown time between scale-out events in seconds
		/// </summary>
		public int? ScaleOutCooldown { get; set; }
		
		/// <summary>
		/// Cooldown time between scale-in events in seconds
		/// </summary>
		public int? ScaleInCooldown { get; set; }
		
		/// <summary>
		/// Pool sizing strategy
		/// </summary>
		public PoolSizeStrategy? SizeStrategy { get; set; }
		
		/// <summary>
		/// Settings for lease utilization pool sizing strategy (if used)
		/// </summary>
		public LeaseUtilizationSettings? LeaseUtilizationSettings { get; set; }
		
		/// <summary>
		/// Settings for job queue pool sizing strategy (if used) 
		/// </summary>
		public JobQueueSettings? JobQueueSettings { get; set; }

		/// <summary>
		/// The minimum nunmber of agents to retain in this pool
		/// </summary>
		public int? MinAgents { get; set; }

		/// <summary>
		/// The minimum number of idle agents in this pool, if autoscaling is enabled
		/// </summary>
		public int? NumReserveAgents { get; set; }

		/// <summary>
		/// Properties for the new pool
		/// </summary>
		public Dictionary<string, string>? Properties { get; set; }
	}

	/// <summary>
	/// Response from creating a new pool
	/// </summary>
	public class CreatePoolResponse
	{
		/// <summary>
		/// Unique id for the new pool
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Unique id for the new pool</param>
		public CreatePoolResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Parameters to update a pool
	/// </summary>
	public class UpdatePoolRequest
	{
		/// <summary>
		/// Optional new name for the pool
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// Requirements for this pool
		/// </summary>
		public Condition? Condition { get; set; }

		/// <summary>
		/// Whether to enable autoscaling for this pool
		/// </summary>
		public bool? EnableAutoscaling { get; set; }
		
		/// <summary>
		/// Cooldown time between scale-out events in seconds
		/// </summary>
		public int? ScaleOutCooldown { get; set; }
		
		/// <summary>
		/// Cooldown time between scale-in events in seconds
		/// </summary>
		public int? ScaleInCooldown { get; set; }
		
		/// <summary>
		/// Pool sizing strategy
		/// </summary>
		public PoolSizeStrategy? SizeStrategy { get; set; }
		
		/// <summary>
		/// Settings for lease utilization pool sizing strategy (if used)
		/// </summary>
		public LeaseUtilizationSettings? LeaseUtilizationSettings { get; set; }
		
		/// <summary>
		/// Settings for job queue pool sizing strategy (if used) 
		/// </summary>
		public JobQueueSettings? JobQueueSettings { get; set; }

		/// <summary>
		/// The minimum nunmber of agents to retain in this pool
		/// </summary>
		public int? MinAgents { get; set; }

		/// <summary>
		/// The minimum number of idle agents in this pool, if autoscaling is enabled
		/// </summary>
		public int? NumReserveAgents { get; set; }

		/// <summary>
		/// Properties to update for the pool. Properties set to null will be removed.
		/// </summary>
		public Dictionary<string, string?>? Properties { get; set; }
	}

	/// <summary>
	/// Parameters to update a pool
	/// </summary>
	public class BatchUpdatePoolRequest
	{
		/// <summary>
		///  ID of the pool to update
		/// </summary>
		[Required]
		public string Id { get; set; } = null!;

		/// <summary>
		/// Optional new name for the pool
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// Properties to update for the pool. Properties set to null will be removed.
		/// </summary>
		public Dictionary<string, string?>? Properties { get; set; }
	}

	/// <summary>
	/// Response describing a pool
	/// </summary>
	public class GetPoolResponse
	{
		/// <summary>
		/// Unique id of the pool
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Name of the pool
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Condition for agents to be auto-added to the pool
		/// </summary>
		public Condition? Condition { get; set; }

		/// <summary>
		/// Whether to enable autoscaling for this pool
		/// </summary>
		public bool EnableAutoscaling { get; set; }
		
		/// <summary>
		/// Cooldown time between scale-out events in seconds
		/// </summary>
		public int? ScaleOutCooldown { get; set; }
		
		/// <summary>
		/// Cooldown time between scale-in events in seconds
		/// </summary>
		public int? ScaleInCooldown { get; set; }
		
		/// <summary>
		/// Pool sizing strategy to be used for this pool
		/// </summary>
		public PoolSizeStrategy? SizeStrategy { get; set; }
		
		/// <summary>
		/// Settings for lease utilization pool sizing strategy (if used)
		/// </summary>
		public LeaseUtilizationSettings? LeaseUtilizationSettings { get; set; }
		
		/// <summary>
		/// Settings for job queue pool sizing strategy (if used) 
		/// </summary>
		public JobQueueSettings? JobQueueSettings { get; set; }

		/// <summary>
		/// The minimum nunmber of agents to retain in this pool
		/// </summary>
		public int? MinAgents { get; set; }

		/// <summary>
		/// The minimum number of idle agents in this pool, if autoscaling is enabled
		/// </summary>
		public int? NumReserveAgents { get; set; }

		/// <summary>
		/// List of workspaces that this agent contains
		/// </summary>
		public List<GetAgentWorkspaceResponse> Workspaces { get; set; }

		/// <summary>
		/// Arbitrary properties for this pool.
		/// </summary>
		public IReadOnlyDictionary<string, string> Properties { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Pool">The pool to construct from</param>
		public GetPoolResponse(IPool Pool)
		{
			this.Id = Pool.Id.ToString();
			this.Name = Pool.Name;
			this.Condition = Pool.Condition;
			this.EnableAutoscaling = Pool.EnableAutoscaling;
			this.ScaleOutCooldown = Pool.ScaleOutCooldown == null ? null : (int)Pool.ScaleOutCooldown.Value.TotalSeconds;
			this.ScaleInCooldown = Pool.ScaleInCooldown == null ? null : (int)Pool.ScaleInCooldown.Value.TotalSeconds;
			this.SizeStrategy = Pool.SizeStrategy;
			this.LeaseUtilizationSettings = Pool.LeaseUtilizationSettings == null ? null : new LeaseUtilizationSettings(Pool.LeaseUtilizationSettings);
			this.JobQueueSettings = Pool.JobQueueSettings == null ? null : new JobQueueSettings(Pool.JobQueueSettings);
			this.MinAgents = Pool.MinAgents;
			this.NumReserveAgents = Pool.NumReserveAgents;
			this.Workspaces = Pool.Workspaces.Select(x => new GetAgentWorkspaceResponse(x)).ToList();
			this.Properties = Pool.Properties;
		}
	}
}
