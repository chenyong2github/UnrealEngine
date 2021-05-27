// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Tasks.Impl
{
	/// <summary>
	/// Generates tasks telling agents to sync their workspaces
	/// </summary>
	public sealed class ConformTaskSource : TaskSourceBase<ConformTask>, IHostedService, IDisposable
	{
		DatabaseService DatabaseService;
		IAgentCollection AgentCollection;
		PoolService PoolService;
		SingletonDocument<ConformList> ConformList;
		PerforceLoadBalancer PerforceLoadBalancer;
		ILogFileService LogService;
		ILogger Logger;
		ElectedTick? TickConformList;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		/// <param name="AgentCollection"></param>
		/// <param name="PoolService"></param>
		/// <param name="LogService"></param>
		/// <param name="PerforceLoadBalancer"></param>
		/// <param name="Logger"></param>
		public ConformTaskSource(DatabaseService DatabaseService, IAgentCollection AgentCollection, PoolService PoolService, ILogFileService LogService, PerforceLoadBalancer PerforceLoadBalancer, ILogger<ConformTaskSource> Logger)
		{
			this.DatabaseService = DatabaseService;
			this.AgentCollection = AgentCollection;
			this.PoolService = PoolService;
			this.ConformList = new SingletonDocument<ConformList>(DatabaseService);
			this.PerforceLoadBalancer = PerforceLoadBalancer;
			this.LogService = LogService;
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			TickConformList?.Dispose();
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken CancellationToken)
		{
			if (TickConformList == null)
			{
				TickConformList = new ElectedTick(DatabaseService, new ObjectId("60afc5cf555a9a76aff0a50c"), CleanConformListAsync, TimeSpan.FromMinutes(0.0), Logger);
			}
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken CancellationToken)
		{
			if (TickConformList != null)
			{
				await TickConformList.DisposeAsync();
				TickConformList = null;
			}
		}

		/// <summary>
		/// Clean up the conform list of any outdated entries
		/// </summary>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>Async task</returns>
		async Task CleanConformListAsync(CancellationToken CancellationToken)
		{
			DateTime UtcNow = DateTime.UtcNow;
			DateTime LastCheckTimeUtc = UtcNow - TimeSpan.FromMinutes(30.0);

			// Get the current state of the conform list
			ConformList List = await ConformList.GetAsync();

			// Update any leases that are older than LastCheckTimeUtc
			Dictionary<ObjectId, bool> RemoveLeases = new Dictionary<ObjectId, bool>();
			foreach (ConformListEntry Entry in List.Entries)
			{
				if (Entry.LastCheckTimeUtc < LastCheckTimeUtc)
				{
					IAgent? Agent = await AgentCollection.GetAsync(Entry.AgentId);

					bool Remove = false;
					if (Agent == null || !Agent.Leases.Any(x => x.Id == Entry.LeaseId))
					{
						Logger.LogWarning("Removing invalid lease from conform list: {AgentId} lease {LeaseId}", Entry.AgentId, Entry.LeaseId);
						Remove = true;
					}

					RemoveLeases[Entry.LeaseId] = Remove;
				}
			}

			// If there's anything to change, update the list
			if (RemoveLeases.Count > 0)
			{
				await ConformList.UpdateAsync(List => UpdateConformList(List, UtcNow, RemoveLeases));
			}
		}

		/// <summary>
		/// Remove items from the conform list, and update timestamps for them
		/// </summary>
		/// <param name="List">The list to update</param>
		/// <param name="UtcNow">Current time</param>
		/// <param name="RemoveLeases">List of leases to update. Entries with values set to true will be removed, entries with values set to false will have their timestamp updated.</param>
		static void UpdateConformList(ConformList List, DateTime UtcNow, Dictionary<ObjectId, bool> RemoveLeases)
		{
			for (int Idx = 0; Idx < List.Entries.Count; Idx++)
			{
				bool Remove;
				if (RemoveLeases.TryGetValue(List.Entries[Idx].LeaseId, out Remove))
				{
					if (Remove)
					{
						List.Entries.RemoveAt(Idx--);
					}
					else if (List.Entries[Idx].LastCheckTimeUtc < UtcNow)
					{
						List.Entries[Idx].LastCheckTimeUtc = UtcNow;
					}
				}
			}
		}

		/// <inheritdoc/>
		public override async Task<ITaskListener?> SubscribeAsync(IAgent Agent)
		{
			DateTime UtcNow = DateTime.UtcNow;
			if (!await IsConformPendingAsync(Agent, UtcNow))
			{
				return null;
			}

			if (Agent.Leases.Count == 0)
			{
				ObjectId LeaseId = ObjectId.GenerateNewId();
				if (await AllocateConformLeaseAsync(Agent.Id, LeaseId))
				{
					ConformTask Task = new ConformTask();
					if (!await GetWorkspacesAsync(Agent, Task.Workspaces))
					{
						await ReleaseConformLeaseAsync(LeaseId);
						return null;
					}

					ILogFile Log = await LogService.CreateLogFileAsync(ObjectId.Empty, Agent.SessionId, LogType.Json);
					Task.LogId = Log.Id.ToString();

					byte[] Payload = Any.Pack(Task).ToByteArray();

					AgentLease Lease = new AgentLease(LeaseId, "Updating workspaces", null, null, Log.Id, LeaseState.Pending, Payload, new AgentRequirements(), null);
					return TaskSubscription.FromResult(Lease);
				}
			}

			if (Agent.RequestConform)
			{
				return TaskSubscription.FromResult(null);
			}

			return null;
		}

		/// <inheritdoc/>
		public async Task<bool> GetWorkspacesAsync(IAgent Agent, IList<HordeCommon.Rpc.Messages.AgentWorkspace> Workspaces)
		{
			Globals Globals = await DatabaseService.GetGlobalsAsync();

			HashSet<AgentWorkspace> ConformWorkspaces = await PoolService.GetWorkspacesAsync(Agent, DateTime.UtcNow);
			foreach (AgentWorkspace ConformWorkspace in ConformWorkspaces)
			{
				if (!await Agent.TryAddWorkspaceMessage(ConformWorkspace, Globals, PerforceLoadBalancer, Workspaces))
				{
					return false;
				}
			}

			return true;
		}

		/// <inheritdoc/>
		protected override Task AbortTaskAsync(IAgent Agent, ObjectId LeaseId, ConformTask Task)
		{
			return ReleaseConformLeaseAsync(LeaseId);
		}

		/// <summary>
		/// Atempt to allocate a conform resource for the given lease
		/// </summary>
		/// <param name="AgentId">The agent id</param>
		/// <param name="LeaseId">The lease id</param>
		/// <returns>True if the resource was allocated, false otherwise</returns>
		private async Task<bool> AllocateConformLeaseAsync(AgentId AgentId, ObjectId LeaseId)
		{
			Globals Globals = await DatabaseService.GetGlobalsAsync();
			for (; ; )
			{
				ConformList CurrentValue = await ConformList.GetAsync();
				if (Globals.MaxConformCount != 0 && CurrentValue.Entries.Count >= Globals.MaxConformCount)
				{
					return false;
				}

				ConformListEntry Entry = new ConformListEntry();
				Entry.AgentId = AgentId;
				Entry.LeaseId = LeaseId;
				Entry.LastCheckTimeUtc = DateTime.UtcNow;
				CurrentValue.Entries.Add(Entry);

				if (await ConformList.TryUpdateAsync(CurrentValue))
				{
					Logger.LogInformation("Added conform lease {LeaseId}", LeaseId);
					return true;
				}
			}
		}

		/// <summary>
		/// Terminate a conform lease
		/// </summary>
		/// <param name="LeaseId">The lease id</param>
		/// <returns>Async task</returns>
		public async Task ReleaseConformLeaseAsync(ObjectId LeaseId)
		{
			for (; ; )
			{
				ConformList CurrentValue = await ConformList.GetAsync();
				if (CurrentValue.Entries.RemoveAll(x => x.LeaseId == LeaseId) == 0)
				{
					Logger.LogInformation("Conform lease {LeaseId} is not in singelton", LeaseId);
					break;
				}
				if (await ConformList.TryUpdateAsync(CurrentValue))
				{
					Logger.LogInformation("Removed conform lease {LeaseId}", LeaseId);
					break;
				}
			}
		}

		/// <summary>
		/// Determine if an agent should be conformed
		/// </summary>
		/// <param name="Agent">The agent to test</param>
		/// <param name="UtcNow">Current time</param>
		/// <returns>True if the agent should be conformed</returns>
		private async Task<bool> IsConformPendingAsync(IAgent Agent, DateTime UtcNow)
		{
			// If a conform was manually requested, allow it to run even if the agent is disabled
			if (Agent.RequestConform)
			{
				return !IsConformCoolDownPeriod(Agent, UtcNow);
			}

			// Otherwise only run if the agent is enabled
			if (Agent.Enabled)
			{
				// If we've attempted (and failed) a conform, run again after a certain time
				if (Agent.ConformAttemptCount.HasValue)
				{
					return !IsConformCoolDownPeriod(Agent, UtcNow);
				}

				// Always run a conform every 24h
				if (UtcNow > Agent.LastConformTime + TimeSpan.FromDays(1.0))
				{
					return true;
				}

				// Check if the workspaces have changed (first check against a cached list of workspaces, then an accurate one)
				HashSet<AgentWorkspace> Workspaces = await PoolService.GetWorkspacesAsync(Agent, UtcNow - TimeSpan.FromSeconds(30.0));
				if (!Workspaces.SetEquals(Agent.Workspaces))
				{
					Workspaces = await PoolService.GetWorkspacesAsync(Agent, UtcNow);
					if (!Workspaces.SetEquals(Agent.Workspaces))
					{
						return true;
					}
				}
			}
			return false;
		}

		/// <summary>
		/// Determines if the given agent is in a cooldown period, waiting to retry after a failed conform
		/// </summary>
		/// <param name="Agent">The agent to test</param>
		/// <param name="UtcNow">Current time</param>
		/// <returns></returns>
		private static bool IsConformCoolDownPeriod(IAgent Agent, DateTime UtcNow)
		{
			if (!Agent.ConformAttemptCount.HasValue)
			{
				return false;
			}

			TimeSpan RetryTime;
			switch (Agent.ConformAttemptCount.Value)
			{
				case 1:
					RetryTime = TimeSpan.FromMinutes(5.0);
					break;
				case 2:
					RetryTime = TimeSpan.FromMinutes(20.0);
					break;
				case 3:
					RetryTime = TimeSpan.FromHours(1.0);
					break;
				default:
					RetryTime = TimeSpan.FromHours(6.0);
					break;
			}
			return UtcNow < Agent.LastConformTime + RetryTime;
		}
	}
}
