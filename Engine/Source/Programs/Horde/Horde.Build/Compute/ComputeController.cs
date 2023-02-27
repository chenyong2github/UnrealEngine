// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using Horde.Build.Acls;
using Horde.Build.Server;
using Horde.Build.Utilities;
using HordeCommon.Rpc.Tasks;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;

namespace Horde.Build.Compute
{
	using ByteString = Google.Protobuf.ByteString;

	/// <summary>
	/// Request a machine to execute compute requests
	/// </summary>
	public class AddComputeTaskRequest
	{
		/// <summary>
		/// Condition to identify machines that can execute the request
		/// </summary>
		public Requirements? Requirements { get; set; }

		/// <summary>
		/// Port to connect on
		/// </summary>
		public int? RemotePort { get; set; }

		/// <summary>
		/// Cryptographic nonce to identify the request, as a hex string
		/// </summary>
		public string Nonce { get; set; } = String.Empty;

		/// <summary>
		/// AES key for the channel, as a hex string
		/// </summary>
		public string AesKey { get; set; } = String.Empty;

		/// <summary>
		/// AES IV for the channel, as a hex string
		/// </summary>
		public string AesIv { get; set; } = String.Empty;
	}

	/// <summary>
	/// Request a machine to execute compute requests
	/// </summary>
	public class AddComputeTasksRequest
	{
		/// <summary>
		/// Condition to identify machines that can execute the request
		/// </summary>
		public Requirements? Requirements { get; set; }

		/// <summary>
		/// Port to connect on
		/// </summary>
		public int RemotePort { get; set; }

		/// <summary>
		/// List of tasks to add
		/// </summary>
		public List<AddComputeTaskRequest> Tasks { get; set; } = new List<AddComputeTaskRequest>();
	}

	/// <summary>
	/// Gets information about the configured compute tunnel
	/// </summary>
	public class GetComputeTunnelResponse
	{
		/// <summary>
		/// IP address of the server
		/// </summary>
		public string? Ip { get; set; }

		/// <summary>
		/// Listening port for the initiator
		/// </summary>
		public int InitiatorPort { get; set; }

		/// <summary>
		/// Listening port for the remote
		/// </summary>
		public int RemotePort { get; set; }
	}

	/// <summary>
	/// Controller for the /api/v2/compute endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ComputeControllerV2 : HordeControllerBase
	{
		readonly ComputeService _computeService;
		readonly IOptionsSnapshot<GlobalConfig> _globalConfig;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeControllerV2(ComputeService computeService, IOptionsSnapshot<GlobalConfig> globalConfig)
		{
			_computeService = computeService;
			_globalConfig = globalConfig;
		}

		/// <summary>
		/// Add tasks to be executed remotely
		/// </summary>
		/// <param name="clusterId">Id of the compute cluster</param>
		/// <param name="request">The request parameters</param>
		/// <returns></returns>
		[HttpPost]
		[Authorize]
		[Route("/api/v2/compute/{clusterId}")]
		public ActionResult AddTasksAsync(ClusterId clusterId, [FromBody] AddComputeTasksRequest request)
		{
			ComputeClusterConfig? clusterConfig;
			if (!_globalConfig.Value.TryGetComputeCluster(clusterId, out clusterConfig))
			{
				return NotFound(clusterId);
			}
			if(!clusterConfig.Authorize(AclAction.AddComputeTasks, User))
			{
				return Forbid(AclAction.AddComputeTasks, clusterId);
			}

			IPAddress? remoteIp = HttpContext.Connection.RemoteIpAddress;
			if (remoteIp == null)
			{
				return BadRequest("Missing remote IP address");
			}

			foreach (AddComputeTaskRequest taskRequest in request.Tasks)
			{
				Requirements requirements = taskRequest.Requirements ?? request.Requirements ?? new Requirements();

				ComputeTask computeTask = new ComputeTask();
				computeTask.RemoteIp = remoteIp.ToString();
				computeTask.RemotePort = taskRequest.RemotePort ?? request.RemotePort;
				computeTask.Nonce = ByteString.CopyFrom(StringUtils.ParseHexString(taskRequest.Nonce));
				computeTask.AesKey = ByteString.CopyFrom(StringUtils.ParseHexString(taskRequest.AesKey));
				computeTask.AesIv = ByteString.CopyFrom(StringUtils.ParseHexString(taskRequest.AesIv));

				_computeService.AddRequest(clusterId, requirements, computeTask);
			}
			return Ok();
		}

		/// <summary>
		/// Add tasks to be executed remotely
		/// </summary>
		/// <param name="clusterId">Id of the compute cluster</param>
		/// <returns></returns>
		[HttpGet]
		[Authorize]
		[Route("/api/v2/compute/{clusterId}/tunnel")]
		public ActionResult<GetComputeTunnelResponse> GetTunnelInfoAsync(ClusterId clusterId)
		{
			if (!_globalConfig.Value.TryGetComputeCluster(clusterId, out _))
			{
				return NotFound(clusterId);
			}

			ServerSettings settings = _globalConfig.Value.ServerSettings;
			if (settings.ComputeInitiatorPort == 0 || settings.ComputeRemotePort == 0)
			{
				return NotFound("Tunnelling is not configured on the server");
			}

			GetComputeTunnelResponse response = new GetComputeTunnelResponse();
			response.Ip = HttpContext.Connection.LocalIpAddress?.ToString();
			response.InitiatorPort = _globalConfig.Value.ServerSettings.ComputeInitiatorPort;
			response.RemotePort = _globalConfig.Value.ServerSettings.ComputeRemotePort;
			return response;
		}
	}
}
