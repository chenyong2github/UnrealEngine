// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Services.Impl
{
	/// <summary>
	/// Default implementation of <see cref="IFleetManager"/>
	/// </summary>
	public class DefaultFleetManager : IFleetManager
	{
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Logger">Logging device</param>
		public DefaultFleetManager(ILogger<DefaultFleetManager> Logger)
		{
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public Task ExpandPool(IPool Pool, IReadOnlyList<IAgent> Agents, int Count)
		{
			Logger.LogInformation("Expand pool {PoolId} by {Count} agents", Pool.Id, Count);
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public Task ShrinkPool(IPool Pool, IReadOnlyList<IAgent> Agents, int Count)
		{
			Logger.LogInformation("Shrink pool {PoolId} by {Count} agents", Pool.Id, Count);
			return Task.CompletedTask;
		}
	}
}
