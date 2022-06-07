// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using Horde.Build.Models;

namespace Horde.Build.Services
{
	/// <summary>
	/// Service to manage a fleet of machines
	/// </summary>
	public interface IFleetManager
	{
		/// <summary>
		/// Expand the given pool
		/// </summary>
		/// <param name="pool">Pool to resize</param>
		/// <param name="agents">Current list of agents in the pool</param>
		/// <param name="count">Number of agents to add</param>
		/// <returns>Async task</returns>
		Task ExpandPoolAsync(IPool pool, IReadOnlyList<IAgent> agents, int count);

		/// <summary>
		/// Shrink the given pool
		/// </summary>
		/// <param name="pool">Pool to resize</param>
		/// <param name="agents">Current list of agents in the pool</param>
		/// <param name="count">Number of agents to remove</param>
		/// <returns>Async task</returns>
		Task ShrinkPoolAsync(IPool pool, IReadOnlyList<IAgent> agents, int count);

		/// <summary>
		/// Returns the number of stopped instances in the given pool
		/// </summary>
		/// <param name="pool">Pool to resize</param>
		/// <returns>Async task</returns>
		Task<int> GetNumStoppedInstancesAsync(IPool pool);
	}
}

