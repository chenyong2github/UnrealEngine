// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Horde.Build.Agents;
using Horde.Build.Agents.Leases;
using Horde.Build.Tasks;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Compute
{
	/// <summary>
	/// Dispatches requests for compute resources
	/// </summary>
	public class ComputeTaskSource : TaskSourceBase<ComputeTask>
	{
		/// <inheritdoc/>
		public override string Type => "Compute";

		/// <inheritdoc/>
		public override TaskSourceFlags Flags => TaskSourceFlags.None;

		readonly ComputeService _computeService;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeTaskSource(ComputeService computeService, ILogger<ComputeTaskSource> logger)
		{
			_computeService = computeService;
			_logger = logger;
		}

		/// <inheritdoc/>
		public override async Task<Task<AgentLease?>> AssignLeaseAsync(IAgent agent, CancellationToken cancellationToken)
		{
			while (!cancellationToken.IsCancellationRequested)
			{
				ComputeRequest? request = _computeService.PopRequest(agent);
				if (request != null)
				{
					string leaseName = $"Compute for {request.Task.RemoteIp}";
					byte[] payload = Any.Pack(request.Task).ToByteArray();

					AgentLease lease = new AgentLease(LeaseId.GenerateNewId(), leaseName, null, null, null, LeaseState.Pending, request.Requirements.Resources, request.Requirements.Exclusive, payload);
					_logger.LogInformation("Created compute lease for agent {AgentId} and remote {RemoteIp}:{RemotePort}", agent.Id, request.Task.RemoteIp, request.Task.RemotePort);
					return Task.FromResult<AgentLease?>(lease);
				}

				await AsyncUtils.DelayNoThrow(TimeSpan.FromSeconds(5.0), cancellationToken);
			}
			return Task.FromResult<AgentLease?>(null);
		}
	}
}
