// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using HordeServer.Models;
using HordeServer.Utilities;

namespace Horde.Build.Fleet.Autoscale
{
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Available pool sizing strategies
	/// </summary>
	public enum PoolSizeStrategy
	{
		/// <summary>
		/// Strategy based on lease utilization
		/// <see cref="Horde.Build.Fleet.Autoscale.LeaseUtilizationStrategy"/>
		/// </summary>
		LeaseUtilization,
		
		/// <summary>
		/// Strategy based on size of job build queue
		/// <see cref="Horde.Build.Fleet.Autoscale.JobQueueStrategy"/> 
		/// </summary>
		JobQueue,
		
		/// <summary>
		/// No-op strategy used as fallback/default behavior
		/// <see cref="Horde.Build.Fleet.Autoscale.NoOpPoolSizeStrategy"/> 
		/// </summary>
		NoOp
	}

	/// <summary>
	/// Class for specifying and grouping data together required for calculating pool size
	/// </summary>
	public class PoolSizeData
	{
		/// <summary>
		/// Pool being resized
		/// </summary>
		public IPool Pool { get; }
		
		/// <summary>
		/// All agents currently associated with the pool
		/// </summary>
		public List<IAgent> Agents { get; }

		/// <summary>
		/// The desired agent count (calculated and updated once the strategy has been run, null otherwise)
		/// </summary>
		public int? DesiredAgentCount  { get; }
		
		/// <summary>
		/// Human-readable text describing the status of the pool (data the sizing is based on etc)
		/// </summary>
		public string StatusMessage { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Pool"></param>
		/// <param name="Agents"></param>
		/// <param name="DesiredAgentCount"></param>
		/// <param name="StatusMessage"></param>
		public PoolSizeData(IPool Pool, List<IAgent> Agents, int? DesiredAgentCount, string StatusMessage = "N/A")
		{
			this.Pool = Pool;
			this.Agents = Agents;
			this.DesiredAgentCount = DesiredAgentCount;
			this.StatusMessage = StatusMessage;
		}

		/// <summary>
		/// Copy the object, inheriting any unspecified values from current instance
		/// Needed because the class is immutable 
		/// </summary>
		/// <param name="Pool"></param>
		/// <param name="Agents"></param>
		/// <param name="DesiredAgentCount"></param>
		/// <param name="StatusMessage"></param>
		/// <returns>A new copy</returns>
		public PoolSizeData Copy(IPool? Pool = null, List<IAgent>? Agents = null, int? DesiredAgentCount = null, string? StatusMessage = null)
		{
			return new PoolSizeData(Pool ?? this.Pool, Agents ?? this.Agents, DesiredAgentCount ?? this.DesiredAgentCount, StatusMessage ?? this.StatusMessage);
		}
	}
	
	/// <summary>
	/// Interface for different agent pool sizing strategies
	/// </summary>
	public interface IPoolSizeStrategy
	{
		/// <summary>
		/// Calculate the adequate number of agents to be online for given pools
		/// </summary>
		/// <param name="Pools">Pools including attached agents</param>
		/// <returns></returns>
		Task<List<PoolSizeData>> CalcDesiredPoolSizesAsync(List<PoolSizeData> Pools);
		
		/// <summary>
		/// Name of the strategy
		/// </summary>
		string Name { get; }
	}
	
	/// <summary>
	/// No-operation strategy that won't resize pools, just return the existing count.
	/// Used to ensure there's always a strategy available for dependency injection, even if it does nothing.
	/// </summary>
	public class NoOpPoolSizeStrategy : IPoolSizeStrategy
	{
		/// <inheritdoc/>
		public Task<List<PoolSizeData>> CalcDesiredPoolSizesAsync(List<PoolSizeData> Pools)
		{
			List<PoolSizeData> Result = Pools.Select(x => new PoolSizeData(x.Pool, x.Agents, x.Agents.Count, "(no-op)")).ToList();
			return Task.FromResult(Result);
		}

		/// <inheritdoc/>
		public string Name { get; } = "NoOp";
	}
}
