// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
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
	public class PoolUpdateService : TickedBackgroundService
	{
		/// <summary>
		/// Collection of pool documents
		/// </summary>
		IPoolCollection Pools;

		/// <summary>
		/// Collection of stream documents
		/// </summary>
		IStreamCollection Streams;

		/// <summary>
		/// The logger instance for this service
		/// </summary>
		ILogger<PoolService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Pools">Collection of pool documents</param>
		/// <param name="Streams">Collection of stream documents</param>
		/// <param name="Logger">Logger instance</param>
		public PoolUpdateService(IPoolCollection Pools, IStreamCollection Streams, ILogger<PoolService> Logger)
			: base(TimeSpan.FromSeconds(30.0), Logger)
		{
			this.Pools = Pools;
			this.Streams = Streams;
			this.Logger = Logger;
		}

		/// <summary>
		/// Execute the background task
		/// </summary>
		/// <param name="StoppingToken">Cancellation token for the async task</param>
		/// <returns>Async task</returns>
		protected override async Task TickAsync(CancellationToken StoppingToken)
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
				Dictionary<PoolId, List<AgentWorkspace>> PoolToAgentWorkspaces = new Dictionary<PoolId, List<AgentWorkspace>>();

				// Populate the workspace list from the current stream
				List<IStream> ActiveStreams = await Streams.FindAllAsync();
				foreach (IStream ActiveStream in ActiveStreams)
				{
					foreach (KeyValuePair<string, AgentType> AgentTypePair in ActiveStream.AgentTypes)
					{
						// Create the new agent workspace
						AgentWorkspace? AgentWorkspace;
						if (ActiveStream.TryGetAgentWorkspace(AgentTypePair.Value, out AgentWorkspace))
						{
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
					if (!AgentWorkspace.SetEquals(CurrentPool.Workspaces, NewWorkspaces) || CurrentPool.Workspaces.Count != NewWorkspaces.Count)
					{
						Logger.LogInformation("New workspaces for pool {Pool}:{Workspaces}", CurrentPool.Id, String.Join("", NewWorkspaces.Select(x => $"\n  Identifier=\"{x.Identifier}\", Stream={x.Stream}")));

						IPool? Result = await Pools.TryUpdateAsync(CurrentPool, NewWorkspaces: NewWorkspaces);
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
