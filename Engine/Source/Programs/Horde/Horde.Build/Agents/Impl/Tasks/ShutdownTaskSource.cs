// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Horde.Build.Api;
using Horde.Build.Models;
using Horde.Build.Services;
using Horde.Build.Utilities;
using HordeCommon;
using HordeCommon.Rpc.Tasks;

namespace Horde.Build.Tasks.Impl
{
	using JobId = ObjectId<IJob>;
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;

	class ShutdownTaskSource : TaskSourceBase<ShutdownTask>
	{
		public override string Type => "Shutdown";

		public override TaskSourceFlags Flags => TaskSourceFlags.AllowWhenDisabled | TaskSourceFlags.AllowDuringDowntime;

		readonly ILogFileService _logService;

		public ShutdownTaskSource(ILogFileService logService)
		{
			_logService = logService;
			OnLeaseStartedProperties.Add(nameof(ShutdownTask.LogId), x => new LogId(x.LogId));
		}

		public override async Task<AgentLease?> AssignLeaseAsync(IAgent agent, CancellationToken cancellationToken)
		{
			if (!agent.RequestShutdown)
			{
				return null;
			}
			if (agent.Leases.Count > 0)
			{
				return AgentLease.Drain;
			}

			ILogFile log = await _logService.CreateLogFileAsync(JobId.Empty, agent.SessionId, LogType.Json);

			ShutdownTask task = new ShutdownTask();
			task.LogId = log.Id.ToString();

			byte[] payload = Any.Pack(task).ToByteArray();

			return new AgentLease(LeaseId.GenerateNewId(), "Shutdown", null, null, log.Id, LeaseState.Pending, null, true, payload);
		}
	}
}
