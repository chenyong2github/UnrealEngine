// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.ModelBinding;
using Microsoft.Extensions.Options;
using Microsoft.IdentityModel.Tokens;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IdentityModel.Tokens.Jwt;
using System.Linq;
using System.Reflection;
using System.Security.Claims;
using System.Text.Json;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;

namespace HordeServer.Controllers
{
	using LeaseId = ObjectId<ILease>;
	using PoolId = StringId<IPool>;
	using AgentSoftwareChannelName = StringId<AgentSoftwareChannels>;

	/// <summary>
	/// Controller for the /api/v1/leases endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class LeasesController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the ACL service
		/// </summary>
		AclService AclService;

		/// <summary>
		/// Singleton instance of the agent service
		/// </summary>
		AgentService AgentService;

		/// <summary>
		/// Logger instance
		/// </summary>
		private ILogger<LeasesController> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service singleton</param>
		/// <param name="AgentService">The agent service</param>
		/// <param name="Logger">The controller logger</param>
		public LeasesController(AclService AclService, AgentService AgentService, ILogger<LeasesController> Logger)
		{
			this.AclService = AclService;
			this.AgentService = AgentService;
			this.Logger = Logger;
		}

		/// <summary>
		/// Find all the leases for a particular agent
		/// </summary>
		/// <param name="AgentId">Unique id of the agent to find</param>
		/// <param name="SessionId">The session to query</param>
		/// <param name="StartTime">Start of the time window to consider</param>
		/// <param name="FinishTime">End of the time window to consider</param>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>Sessions </returns>
		[HttpGet]
		[Route("/api/v1/leases")]
		public async Task<ActionResult<List<GetAgentLeaseResponse>>> FindLeasesAsync([FromQuery] string? AgentId, [FromQuery] string? SessionId, [FromQuery] DateTimeOffset? StartTime, [FromQuery] DateTimeOffset? FinishTime, [FromQuery] int Index = 0, [FromQuery] int Count = 1000)
		{
			AgentId? AgentIdValue = null;
			if (AgentId == null)
			{
				if (!await AclService.AuthorizeAsync(AclAction.ViewLeases, User))
				{
					return Forbid();
				}
			}
			else
			{
				AgentIdValue = new AgentId(AgentId);

				IAgent? Agent = await AgentService.GetAgentAsync(AgentIdValue.Value);
				if (Agent == null)
				{
					return NotFound();
				}
				if (!await AgentService.AuthorizeAsync(Agent, AclAction.ViewLeases, User, null))
				{
					return Forbid();
				}
			}

			List<ILease> Leases = await AgentService.FindLeasesAsync(AgentIdValue, SessionId?.ToObjectId(), StartTime?.UtcDateTime, FinishTime?.UtcDateTime, Index, Count);
			return Leases.ConvertAll(x => new GetAgentLeaseResponse(x));
		}

		/// <summary>
		/// Get info about a particular lease
		/// </summary>
		/// <param name="LeaseId">Unique id of the particular lease</param>
		/// <returns>Lease matching the given id</returns>
		[HttpGet]
		[Route("/api/v1/leases/{LeaseId}")]
		public async Task<ActionResult<GetAgentLeaseResponse>> GetLeaseAsync(LeaseId LeaseId)
		{
			ILease? Lease = await AgentService.GetLeaseAsync(LeaseId);
			if (Lease == null)
			{
				return NotFound();
			}

			IAgent? Agent = await AgentService.GetAgentAsync(Lease.AgentId);
			if (Agent == null)
			{
				return NotFound();
			}
			if (!await AgentService.AuthorizeAsync(Agent, AclAction.ViewLeases, User, null))
			{
				return Forbid();
			}

			return new GetAgentLeaseResponse(Lease);
		}

		/// <summary>
		/// Update a particular lease
		/// </summary>
		/// <param name="LeaseId">Unique id of the particular lease</param>
		/// <param name="Request"></param>
		/// <returns>Lease matching the given id</returns>
		[HttpPut]
		[Route("/api/v1/leases/{LeaseId}")]
		public async Task<ActionResult> UpdateLeaseAsync(LeaseId LeaseId, [FromBody] UpdateLeaseRequest Request)
		{
			// only update supported right now is abort
			if (!Request.Aborted.HasValue || !Request.Aborted.Value)
			{
				return Ok();
			}

			ILease? Lease = await AgentService.GetLeaseAsync(LeaseId);
			if (Lease == null)
			{
				return NotFound();
			}

			IAgent? Agent = await AgentService.GetAgentAsync(Lease.AgentId);
			if (Agent == null)
			{
				return NotFound();
			}

			if (!await AgentService.AuthorizeAsync(Agent, AclAction.AdminWrite, User, null))
			{
				return Forbid();
			}

			AgentLease? AgentLease = Agent.Leases.FirstOrDefault(x => x.Id == LeaseId);

			if (AgentLease == null)
			{
				return NotFound();
			}

			if (!AgentLease.IsConformLease())
			{
				Logger.LogError("Lease abort only supported on conform leases for now, {LeaseId}", LeaseId);
				return Ok();
			}

			await AgentService.CancelLeaseAsync(Agent, LeaseId);

			return Ok();
		}

	}
}
