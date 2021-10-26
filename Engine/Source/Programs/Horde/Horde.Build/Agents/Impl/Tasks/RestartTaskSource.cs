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
	using JobId = ObjectId<IJob>;
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;

	class RestartTaskSource : TaskSourceBase<RestartTask>
	{
		public override string Type => "Restart";

		ILogFileService LogService;

		public RestartTaskSource(ILogFileService LogService)
		{
			this.LogService = LogService;

			this.OnLeaseStartedProperties.Add(nameof(RestartTask.LogId), x => new LogId(x.LogId));
		}

		public override async Task<AgentLease?> AssignLeaseAsync(IAgent Agent, CancellationToken CancellationToken)
		{
			if (!Agent.RequestRestart)
			{
				return null;
			}
			if (Agent.Leases.Count > 0)
			{
				return AgentLease.Drain;
			}

			ILogFile Log = await LogService.CreateLogFileAsync(JobId.Empty, Agent.SessionId, LogType.Json);

			RestartTask Task = new RestartTask();
			Task.LogId = Log.Id.ToString();

			byte[] Payload = Any.Pack(Task).ToByteArray();

			return new AgentLease(LeaseId.GenerateNewId(), "Restart", null, null, Log.Id, LeaseState.Pending, null, true, Payload);
		}
	}
}
