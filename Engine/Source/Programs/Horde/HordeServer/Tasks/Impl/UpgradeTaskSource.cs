// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;
using AgentSoftwareChannelName = HordeServer.Utilities.StringId<HordeServer.Services.AgentSoftwareChannels>;

namespace HordeServer.Tasks.Impl
{
	class UpgradeTaskSource : ITaskSource
	{
		AgentSoftwareService AgentSoftwareService;
		ILogFileService LogService;
		IClock Clock;

		public MessageDescriptor Descriptor => UpgradeTask.Descriptor;

		public UpgradeTaskSource(AgentSoftwareService AgentSoftwareService, ILogFileService LogService, IClock Clock)
		{
			this.AgentSoftwareService = AgentSoftwareService;
			this.LogService = LogService;
			this.Clock = Clock;
		}

		public async Task<ITaskListener?> SubscribeAsync(IAgent Agent)
		{
			AgentSoftwareVersion? RequiredVersion = await GetRequiredSoftwareVersion(Agent);
			if (RequiredVersion != null && Agent.Version != RequiredVersion)
			{
				AgentLease? Lease = null;
				if (Agent.Leases.Count == 0 && (Agent.LastUpgradeTime == null || Agent.LastUpgradeTime.Value + TimeSpan.FromMinutes(5.0) < Clock.UtcNow || Agent.LastUpgradeVersion != RequiredVersion.ToString()))
				{
					ILogFile LogFile = await LogService.CreateLogFileAsync(ObjectId.Empty, Agent.SessionId, LogType.Json);

					UpgradeTask Task = new UpgradeTask();
					Task.SoftwareId = RequiredVersion.ToString();
					Task.LogId = LogFile.Id.ToString();

					byte[] Payload = Any.Pack(Task).ToByteArray();

					Lease = new AgentLease(ObjectId.GenerateNewId(), $"Upgrade to {RequiredVersion}", null, null, LogFile.Id, LeaseState.Pending, Payload, new AgentRequirements(), null);
				}
				return TaskSubscription.FromResult(Lease);
			}
			return null;
		}

		public Task AbortTaskAsync(IAgent Agent, ObjectId LeaseId, Any Payload)
		{
			return Task.CompletedTask;
		}

		/// <summary>
		/// Determines the client software version that should be installed on an agent
		/// </summary>
		/// <param name="Agent">The agent instance</param>
		/// <returns>Unique id of the client version this agent should be running</returns>
		public async Task<AgentSoftwareVersion?> GetRequiredSoftwareVersion(IAgent Agent)
		{
			AgentSoftwareChannelName ChannelName = Agent.Channel ?? AgentSoftwareService.DefaultChannelName;
			IAgentSoftwareChannel? Channel = await AgentSoftwareService.GetCachedChannelAsync(ChannelName);
			return Channel?.Version;
		}
	}
}
