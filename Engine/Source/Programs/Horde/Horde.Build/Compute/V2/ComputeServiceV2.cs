// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Serialization;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Horde.Build.Agents;
using Horde.Build.Agents.Leases;
using Horde.Build.Agents.Pools;
using Horde.Build.Tasks;
using Horde.Build.Utilities;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Compute.V2
{
	using LeaseId = ObjectId<ILease>;

	/// <summary>
	/// Dispatches remote actions. Does not implement any cross-pod communication to satisfy leases; only agents connected to this server instance will be stored.
	/// </summary>
	public sealed class ComputeServiceV2 : TaskSourceBase<ComputeTaskMessageV2>
	{
		class Request
		{
			public string Ip { get; }
			public int Port { get; }

			public Request(string ip, int port)
			{
				Ip = ip;
				Port = port;
			}
		}

		class RequestQueue
		{
			public IoHash Hash { get; }
			public Requirements Requirements { get; }
			public Queue<Request> Requests { get; } = new Queue<Request>();

			public RequestQueue(IoHash hash, Requirements requirements)
			{
				Hash = hash;
				Requirements = requirements;
			}
		}

		readonly object _lockObject = new object();
		readonly Dictionary<IoHash, RequestQueue> _queues = new Dictionary<IoHash, RequestQueue>();
		readonly ILogger _logger;

		/// <inheritdoc/>
		public override string Type => "ComputeV2";

		/// <inheritdoc/>
		public override TaskSourceFlags Flags => TaskSourceFlags.None;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="logger"></param>
		public ComputeServiceV2(ILogger<ComputeServiceV2> logger)
		{
			_logger = logger;
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="requirements"></param>
		/// <param name="ip"></param>
		/// <param name="port"></param>
		public void AddRequest(Requirements requirements, string ip, int port)
		{
			CbObject obj = CbSerializer.Serialize(requirements);
			IoHash hash = IoHash.Compute(obj.GetView().Span);
			lock (_lockObject)
			{
				RequestQueue? queue;
				if (!_queues.TryGetValue(hash, out queue))
				{
					queue = new RequestQueue(hash, requirements);
					_queues.Add(hash, queue);
				}
				queue.Requests.Enqueue(new Request(ip, port));
			}
		}

		/// <inheritdoc/>
		public override async Task<Task<AgentLease?>> AssignLeaseAsync(IAgent agent, CancellationToken cancellationToken)
		{
			while (!cancellationToken.IsCancellationRequested)
			{
				Request? request = null;
				RequestQueue? requestQueue = null;

				lock (_lockObject)
				{
					foreach ((IoHash hash, RequestQueue queue) in _queues)
					{
						if (agent.MeetsRequirements(queue.Requirements))
						{
							request = queue.Requests.Dequeue();
							requestQueue = queue;

							if (queue.Requests.Count == 0)
							{
								_queues.Remove(hash);
							}
							break;
						}
					}
				}

				if (request != null)
				{
					ComputeTaskMessageV2 computeTask = new ComputeTaskMessageV2();
					computeTask.RemoteIp = request.Ip;
					computeTask.RemotePort = request.Port;

					string leaseName = $"Compute lease";
					byte[] payload = Any.Pack(computeTask).ToByteArray();

					AgentLease lease = new AgentLease(LeaseId.GenerateNewId(), leaseName, null, null, null, LeaseState.Pending, requestQueue!.Requirements.Resources, requestQueue.Requirements.Exclusive, payload);
					_logger.LogInformation("Created compute lease for agent {AgentId} and remote {RemoteIp}:{RemotePort}", agent.Id, request.Ip, request.Port);
					return Task.FromResult<AgentLease?>(lease);
				}

				await AsyncUtils.DelayNoThrow(TimeSpan.FromSeconds(5.0), cancellationToken);
			}
			return Task.FromResult<AgentLease?>(null);
		}
	}
}
