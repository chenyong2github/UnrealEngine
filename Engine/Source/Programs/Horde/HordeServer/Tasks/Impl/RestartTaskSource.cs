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
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Tasks.Impl
{
	class RestartTaskSource : TaskSourceBase<RestartTask>
	{
		ILogFileService LogService;

		public RestartTaskSource(ILogFileService LogService)
		{
			this.LogService = LogService;
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

			ILogFile Log = await LogService.CreateLogFileAsync(ObjectId.Empty, Agent.SessionId, LogType.Json);

			RestartTask Task = new RestartTask();
			Task.LogId = Log.Id.ToString();

			byte[] Payload = Any.Pack(Task).ToByteArray();

			return new AgentLease(ObjectId.GenerateNewId(), "Restart", null, null, Log.Id, LeaseState.Pending, null, true, Payload);
		}
	}
}
