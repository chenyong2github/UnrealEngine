// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	/// <summary>
	/// Service to manage a fleet of machines
	/// </summary>
	public interface IFleetManager
	{
		/// <summary>
		/// Expand the given pool
		/// </summary>
		/// <param name="Pool">Pool to resize</param>
		/// <param name="Agents">Current list of agents in the pool</param>
		/// <param name="Count">Number of agents to add</param>
		/// <returns>Async task</returns>
		Task ExpandPoolAsync(IPool Pool, IReadOnlyList<IAgent> Agents, int Count);

		/// <summary>
		/// Shrink the given pool
		/// </summary>
		/// <param name="Pool">Pool to resize</param>
		/// <param name="Agents">Current list of agents in the pool</param>
		/// <param name="Count">Number of agents to remove</param>
		/// <returns>Async task</returns>
		Task ShrinkPoolAsync(IPool Pool, IReadOnlyList<IAgent> Agents, int Count);

		/// <summary>
		/// Returns the number of stopped instances in the given pool
		/// </summary>
		/// <param name="Pool">Pool to resize</param>
		/// <returns>Async task</returns>
		Task<int> GetNumStoppedInstancesAsync(IPool Pool);
	}
}

