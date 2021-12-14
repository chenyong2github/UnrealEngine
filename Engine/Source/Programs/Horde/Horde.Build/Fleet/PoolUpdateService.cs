// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Periodically updates pool documents to contain the correct workspaces
	/// </summary>
	public sealed class PoolUpdateService : IHostedService, IDisposable
	{
		IPoolCollection Pools;
		IStreamCollection Streams;
		ILogger<PoolService> Logger;
		ITicker Ticker;

		/// <summary>
		/// Constructor
		/// </summary>
		public PoolUpdateService(IPoolCollection Pools, IStreamCollection Streams, IClock Clock, ILogger<PoolService> Logger)
		{
			this.Pools = Pools;
			this.Streams = Streams;
			this.Logger = Logger;
			this.Ticker = Clock.AddTicker(TimeSpan.FromSeconds(30.0), UpdatePoolsAsync, Logger);
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken CancellationToken) => Ticker.StartAsync();

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken CancellationToken) => Ticker.StopAsync();

		/// <inheritdoc/>
		public void Dispose() => Ticker.Dispose();

		/// <summary>
		/// Execute the background task
		/// </summary>
		/// <param name="StoppingToken">Cancellation token for the async task</param>
		/// <returns>Async task</returns>
		async ValueTask UpdatePoolsAsync(CancellationToken StoppingToken)
		{
			// Capture the start time for this operation. We use this to attempt to sequence updates to agents, and prevent overriding another server's updates.
			DateTime StartTime = DateTime.UtcNow;

			// Update the list
			bool bRetryUpdate = true;
			while (bRetryUpdate && !StoppingToken.IsCancellationRequested)
			{
				Logger.LogDebug("Updating pool->workspace map");

				// Assume this will be the last iteration
				bRetryUpdate = false;

				// Capture the list of pools at the start of this update
				List<IPool> CurrentPools = await Pools.GetAsync();

				// Lookup table of pool id to workspaces
				HashSet<PoolId> PoolsWithAutoSdk = new HashSet<PoolId>();
				Dictionary<PoolId, List<AgentWorkspace>> PoolToAgentWorkspaces = new Dictionary<PoolId, List<AgentWorkspace>>();

				// Populate the workspace list from the current stream
				List<IStream> ActiveStreams = await Streams.FindAllAsync();
				foreach (IStream ActiveStream in ActiveStreams)
				{
					foreach (KeyValuePair<string, AgentType> AgentTypePair in ActiveStream.AgentTypes)
					{
						// Create the new agent workspace
						(AgentWorkspace, bool)? Result;
						if (ActiveStream.TryGetAgentWorkspace(AgentTypePair.Value, out Result))
						{
							(AgentWorkspace AgentWorkspace, bool UseAutoSdk) = Result.Value;
							AgentType AgentType = AgentTypePair.Value;

							// Find or add a list of workspaces for this pool
							List<AgentWorkspace>? AgentWorkspaces;
							if (!PoolToAgentWorkspaces.TryGetValue(AgentType.Pool, out AgentWorkspaces))
							{
								AgentWorkspaces = new List<AgentWorkspace>();
								PoolToAgentWorkspaces.Add(AgentType.Pool, AgentWorkspaces);
							}

							// Add it to the list
							if (!AgentWorkspaces.Contains(AgentWorkspace))
							{
								AgentWorkspaces.Add(AgentWorkspace);
							}
							if (UseAutoSdk)
							{
								PoolsWithAutoSdk.Add(AgentType.Pool);
							}
						}
					}
				}

				// Update the list of workspaces for each pool
				foreach (IPool CurrentPool in CurrentPools)
				{
					// Get the new list of workspaces for this pool
					List<AgentWorkspace>? NewWorkspaces;
					if (!PoolToAgentWorkspaces.TryGetValue(CurrentPool.Id, out NewWorkspaces))
					{
						NewWorkspaces = new List<AgentWorkspace>();
					}

					// Update the pools document
					bool UseAutoSdk = PoolsWithAutoSdk.Contains(CurrentPool.Id);
					if (!AgentWorkspace.SetEquals(CurrentPool.Workspaces, NewWorkspaces) || CurrentPool.Workspaces.Count != NewWorkspaces.Count || CurrentPool.UseAutoSdk != UseAutoSdk)
					{
						Logger.LogInformation("New workspaces for pool {Pool}:{Workspaces}", CurrentPool.Id, String.Join("", NewWorkspaces.Select(x => $"\n  Identifier=\"{x.Identifier}\", Stream={x.Stream}")));

						IPool? Result = await Pools.TryUpdateAsync(CurrentPool, NewWorkspaces: NewWorkspaces, NewUseAutoSdk: UseAutoSdk);
						if (Result == null)
						{
							Logger.LogInformation("Pool modified; will retry");
							bRetryUpdate = true;
						}
					}
				}
			}
		}
	}
}
