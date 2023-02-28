// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using Horde.Build.Acls;
using Horde.Build.Server;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;

namespace Horde.Build.Compute
{
	/// <summary>
	/// Request a machine to execute compute requests
	/// </summary>
	public class AssignComputeRequest
	{
		/// <summary>
		/// Condition to identify machines that can execute the request
		/// </summary>
		public Requirements? Requirements { get; set; }
	}

	/// <summary>
	/// Request a machine to execute compute requests
	/// </summary>
	public class AssignComputeResponse
	{
		/// <summary>
		/// IP address of the remote machine
		/// </summary>
		public string Ip { get; set; } = String.Empty;

		/// <summary>
		/// Port number on the remote machine
		/// </summary>
		public int Port { get; set; }

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
	/// Controller for the /api/v2/compute endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ComputeControllerV2 : HordeControllerBase
	{
		readonly ComputeTaskSource _computeTaskSource;
		readonly IOptionsSnapshot<GlobalConfig> _globalConfig;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeControllerV2(ComputeTaskSource computeTaskSource, IOptionsSnapshot<GlobalConfig> globalConfig)
		{
			_computeTaskSource = computeTaskSource;
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
		public ActionResult<AssignComputeResponse> AssignComputeResourceAsync(ClusterId clusterId, [FromBody] AssignComputeRequest request)
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

			Requirements requirements = request.Requirements ?? new Requirements();

			ComputeResource? computeResource = _computeTaskSource.TryAllocateResource(clusterId, requirements);
			if (computeResource == null)
			{
				return StatusCode((int)HttpStatusCode.ServiceUnavailable);
			}

			AssignComputeResponse response = new AssignComputeResponse();
			response.Ip = computeResource.Ip.ToString();
			response.Port = computeResource.Port;
			response.Nonce = StringUtils.FormatHexString(computeResource.Task.Nonce.Span);
			response.AesKey = StringUtils.FormatHexString(computeResource.Task.AesKey.Span);
			response.AesIv = StringUtils.FormatHexString(computeResource.Task.AesIv.Span);

			return response;
		}
	}
}
