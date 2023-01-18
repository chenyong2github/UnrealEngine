// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
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
		/// IP address to contact the initiator on.
		/// </summary>
		public string RemoteIp { get; set; } = String.Empty;

		/// <summary>
		/// Port to connect on
		/// </summary>
		public int RemotePort { get; set; } = 4000;
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
			_computeService.AddRequest(requirements, request.RemoteIp, request.RemotePort);
			return Ok();
		}
	}
}
