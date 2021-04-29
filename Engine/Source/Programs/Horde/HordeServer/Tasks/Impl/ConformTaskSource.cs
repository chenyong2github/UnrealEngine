using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Tasks.Impl
{
	/// <summary>
	/// Generates tasks telling agents to sync their workspaces
	/// </summary>
	public class ConformTaskSource : TaskSourceBase<ConformTask>
	{
		DatabaseService DatabaseService;
		PoolService PoolService;
		SingletonDocument<ConformList> ConformList;
		PerforceLoadBalancer PerforceLoadBalancer;
		ILogFileService LogService;
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		/// <param name="PoolService"></param>
		/// <param name="LogService"></param>
		/// <param name="PerforceLoadBalancer"></param>
		/// <param name="Logger"></param>
		public ConformTaskSource(DatabaseService DatabaseService, PoolService PoolService, ILogFileService LogService, PerforceLoadBalancer PerforceLoadBalancer, ILogger<ConformTaskSource> Logger)
		{
			this.DatabaseService = DatabaseService;
			this.PoolService = PoolService;
			this.ConformList = new SingletonDocument<ConformList>(DatabaseService);
			this.PerforceLoadBalancer = PerforceLoadBalancer;
			this.LogService = LogService;
			this.Logger = Logger;
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
				if (await AllocateConformLeaseAsync(LeaseId))
				{
					ConformTask Task = new ConformTask();
					if (!await GetWorkspacesAsync(Agent, Task.Workspaces))
					{
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
		/// <param name="LeaseId">The lease id</param>
		/// <returns>True if the resource was allocated, false otherwise</returns>
		private async Task<bool> AllocateConformLeaseAsync(ObjectId LeaseId)
		{
			for (; ; )
			{
				ConformList CurrentValue = await ConformList.GetAsync();
				if (CurrentValue.MaxCount != 0 && CurrentValue.LeaseIds.Count >= CurrentValue.MaxCount)
				{
					return false;
				}

				CurrentValue.LeaseIds.Add(LeaseId);
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
				if (!CurrentValue.LeaseIds.Remove(LeaseId))
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
