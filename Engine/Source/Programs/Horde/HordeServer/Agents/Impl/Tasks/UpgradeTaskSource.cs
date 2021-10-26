// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Tasks.Impl
{
	using AgentSoftwareChannelName = StringId<AgentSoftwareChannels>;
	using JobId = ObjectId<IJob>;
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;

	class UpgradeTaskSource : TaskSourceBase<UpgradeTask>
	{
		public override string Type => "Upgrade";

		AgentSoftwareService AgentSoftwareService;
		ILogFileService LogService;
		IClock Clock;

		public UpgradeTaskSource(AgentSoftwareService AgentSoftwareService, ILogFileService LogService, IClock Clock)
		{
			this.AgentSoftwareService = AgentSoftwareService;
			this.LogService = LogService;
			this.Clock = Clock;

			this.OnLeaseStartedProperties.Add(nameof(UpgradeTask.LogId), x => new LogId(x.LogId));
		}

		public async override Task<AgentLease?> AssignLeaseAsync(IAgent Agent, CancellationToken CancellationToken)
		{
			string? RequiredVersion = await GetRequiredSoftwareVersion(Agent);
			if (RequiredVersion != null && Agent.Version != RequiredVersion)
			{
				AgentLease? Lease = null;
				if (Agent.Leases.Count == 0 && (Agent.LastUpgradeTime == null || Agent.LastUpgradeTime.Value + TimeSpan.FromMinutes(5.0) < Clock.UtcNow || Agent.LastUpgradeVersion != RequiredVersion.ToString()))
				{
					ILogFile LogFile = await LogService.CreateLogFileAsync(JobId.Empty, Agent.SessionId, LogType.Json);

					UpgradeTask Task = new UpgradeTask();
					Task.SoftwareId = RequiredVersion.ToString();
					Task.LogId = LogFile.Id.ToString();

					byte[] Payload;
					if (Agent.Version == "5.0.0-17425336" || Agent.Version == "5.0.0-17448746")
					{
						Any Any = new Any();
						Any.TypeUrl = "type.googleapis.com/Horde.UpgradeTask";
						Any.Value = Task.ToByteString();
						Payload = Any.ToByteArray();
					}
					else
					{
						Payload = Any.Pack(Task).ToByteArray();
					}

					Lease = new AgentLease(LeaseId.GenerateNewId(), $"Upgrade to {RequiredVersion}", null, null, LogFile.Id, LeaseState.Pending, null, true, Payload);
				}
				return Lease;
			}
			return null;
		}

		/// <summary>
		/// Determines the client software version that should be installed on an agent
		/// </summary>
		/// <param name="Agent">The agent instance</param>
		/// <returns>Unique id of the client version this agent should be running</returns>
		public async Task<string?> GetRequiredSoftwareVersion(IAgent Agent)
		{
			AgentSoftwareChannelName ChannelName = Agent.Channel ?? AgentSoftwareService.DefaultChannelName;
			IAgentSoftwareChannel? Channel = await AgentSoftwareService.GetCachedChannelAsync(ChannelName);
			return Channel?.Version;
		}
	}
}
