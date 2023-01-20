// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Common;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Impl;
using Horde.Build.Acls;
using Horde.Build.Server;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;

namespace Horde.Build.Compute.V2
{
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
		public int RemotePort { get; set; } = 4000;

		/// <summary>
		/// Base-64 encoded cryptographic nonce to identify the request
		/// </summary>
		public string Nonce { get; set; } = String.Empty;

		/// <summary>
		/// Base-64 encoded AES key for the channel
		/// </summary>
		public string AesKey { get; set; } = String.Empty;

		/// <summary>
		/// Base-64 encoded AES IV for the channel
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
		readonly ComputeServiceV2 _computeService;
		readonly IOptionsSnapshot<GlobalConfig> _globalConfig;

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeControllerV2(ComputeServiceV2 computeService, IOptionsSnapshot<GlobalConfig> globalConfig)
		{
			_computeService = computeService;
			_globalConfig = globalConfig;
		}

		/// <summary>
		/// Add tasks to be executed remotely
		/// </summary>
		/// <param name="request">The request parameters</param>
		/// <returns></returns>
		[HttpPost]
		[Authorize]
		[Route("/api/v2/compute")]
		public ActionResult AddTasksAsync([FromBody] AddComputeTaskRequest request)
		{
			if(!_globalConfig.Value.Authorize(AclAction.AddComputeTasks, User))
			{
				return Forbid(AclAction.AddComputeTasks);
			}

			Requirements requirements = request.Requirements ?? new Requirements();
			byte[] nonce = StringUtils.ParseHexString(request.Nonce);
			byte[] aesKey = StringUtils.ParseHexString(request.AesKey);
			byte[] aesIv = StringUtils.ParseHexString(request.AesIv);

			IPAddress? remoteIp = HttpContext.Connection.RemoteIpAddress;
			if (remoteIp == null)
			{
				return BadRequest("Missing remote IP address");
			}

			_computeService.AddRequest(requirements, remoteIp.ToString(), request.RemotePort, nonce, aesKey, aesIv);
			return Ok();
		}
	}
}
