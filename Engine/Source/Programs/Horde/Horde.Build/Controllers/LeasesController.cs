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
using OpenTracing;
using OpenTracing.Util;

namespace HordeServer.Controllers
{
	using LeaseId = ObjectId<ILease>;
	using PoolId = StringId<IPool>;
	using SessionId = ObjectId<ISession>;
	using AgentSoftwareChannelName = StringId<AgentSoftwareChannels>;

	/// <summary>
	/// Controller for the /api/v1/leases endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class LeasesController : HordeControllerBase
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
		/// <param name="Filter">Filter to apply to the results</param>
		/// <returns>Sessions </returns>
		[HttpGet]
		[Route("/api/v1/leases")]
		[ProducesResponseType(200, Type = typeof(List<GetAgentLeaseResponse>))]
		public async Task<ActionResult<List<object>>> FindLeasesAsync([FromQuery] AgentId? AgentId, [FromQuery] SessionId? SessionId, [FromQuery] DateTimeOffset? StartTime, [FromQuery] DateTimeOffset? FinishTime, [FromQuery] int Index = 0, [FromQuery] int Count = 1000, [FromQuery] PropertyFilter? Filter = null)
		{
			GlobalPermissionsCache PermissionsCache = new GlobalPermissionsCache();
			if (AgentId == null)
			{
				if (!await AclService.AuthorizeAsync(AclAction.ViewLeases, User, PermissionsCache))
				{
					return Forbid(AclAction.ViewLeases);
				}
			}
			else
			{
				IAgent? Agent = await AgentService.GetAgentAsync(AgentId.Value);
				if (Agent == null)
				{
					return NotFound(AgentId.Value);
				}
				if (!await AgentService.AuthorizeAsync(Agent, AclAction.ViewLeases, User, PermissionsCache))
				{
					return Forbid(AclAction.ViewLeases, AgentId.Value);
				}
			}

			bool IncludeCosts = await AclService.AuthorizeAsync(AclAction.ViewCosts, User, PermissionsCache);

			List<ILease> Leases = await AgentService.FindLeasesAsync(AgentId, SessionId, StartTime?.UtcDateTime, FinishTime?.UtcDateTime, Index, Count);

			List<object> Responses = new List<object>();

			using (IScope _ = GlobalTracer.Instance.BuildSpan($"GenerateResponses").StartActive())
			{
				Dictionary<AgentId, double?> CachedAgentRates = new Dictionary<AgentId, double?>();
				foreach (ILease Lease in Leases)
				{
					double? AgentRate = null;
					if (IncludeCosts && !CachedAgentRates.TryGetValue(Lease.AgentId, out AgentRate))
					{
						AgentRate = await AgentService.GetRateAsync(Lease.AgentId);
						CachedAgentRates.Add(Lease.AgentId, AgentRate);
					}

					Dictionary<string, string>? Details = AgentService.GetPayloadDetails(Lease.Payload);
					Responses.Add(PropertyFilter.Apply(new GetAgentLeaseResponse(Lease, Details, AgentRate), Filter));
				}
			}

			return Responses;
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
				return NotFound(LeaseId);
			}

			IAgent? Agent = await AgentService.GetAgentAsync(Lease.AgentId);
			if (Agent == null)
			{
				return NotFound(Lease.AgentId);
			}

			GlobalPermissionsCache PermissionsCache = new GlobalPermissionsCache();
			if (!await AgentService.AuthorizeAsync(Agent, AclAction.ViewLeases, User, PermissionsCache))
			{
				return Forbid(AclAction.ViewLeases);
			}

			double? AgentRate = null;
			if (await AclService.AuthorizeAsync(AclAction.ViewCosts, User, PermissionsCache))
			{
				AgentRate = await AgentService.GetRateAsync(Agent.Id);
			}

			Dictionary<string, string>? Details = AgentService.GetPayloadDetails(Lease.Payload);
			return new GetAgentLeaseResponse(Lease, Details, AgentRate);
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
				return NotFound(LeaseId);
			}

			IAgent? Agent = await AgentService.GetAgentAsync(Lease.AgentId);
			if (Agent == null)
			{
				return NotFound(Lease.AgentId);
			}

			if (!await AgentService.AuthorizeAsync(Agent, AclAction.AdminWrite, User, null))
			{
				return Forbid(AclAction.AdminWrite, Lease.AgentId);
			}

			AgentLease? AgentLease = Agent.Leases.FirstOrDefault(x => x.Id == LeaseId);

			if (AgentLease == null)
			{
				return NotFound(Agent.Id, LeaseId);
			}

			if (!AgentLease.IsConformLease())
			{
				return BadRequest("Lease abort only supported on conform leases for now, {LeaseId}", LeaseId);
			}

			await AgentService.CancelLeaseAsync(Agent, LeaseId);
			return Ok();
		}

	}
}
