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

namespace HordeServer.Tasks.Impl
{
	class ShutdownTaskSource : TaskSourceBase<ShutdownTask>
	{
		ILogFileService LogService;

		public ShutdownTaskSource(ILogFileService LogService)
		{
			this.LogService = LogService;
		}

		public override async Task<ITaskListener?> SubscribeAsync(IAgent Agent)
		{
			if (!Agent.RequestShutdown)
			{
				return null;
			}
			if (Agent.Leases.Count > 0)
			{
				return TaskSubscription.FromResult(null);
			}

			ILogFile Log = await LogService.CreateLogFileAsync(ObjectId.Empty, Agent.SessionId, LogType.Json);

			ShutdownTask Task = new ShutdownTask();
			Task.LogId = Log.Id.ToString();

			byte[] Payload = Any.Pack(Task).ToByteArray();

			AgentLease? Lease = new AgentLease(ObjectId.GenerateNewId(), "Shutdown", null, null, Log.Id, LeaseState.Pending, Payload, new AgentRequirements(), null);
			return TaskSubscription.FromResult(Lease);
		}
	}
}
