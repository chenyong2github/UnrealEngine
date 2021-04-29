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

using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;
using AgentSoftwareChannelName = HordeServer.Utilities.StringId<HordeServer.Services.AgentSoftwareChannels>;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/agents endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class AgentsController : ControllerBase
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
		/// The pool service
		/// </summary>
		PoolService PoolService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service singleton</param>
		/// <param name="AgentService">The agent service</param>
		/// <param name="PoolService">The pool service</param>
		public AgentsController(AclService AclService, AgentService AgentService, PoolService PoolService)
		{
			this.AclService = AclService;
			this.AgentService = AgentService;
			this.PoolService = PoolService;
		}

		/// <summary>
		/// Register an agent to perform remote work.
		/// </summary>
		/// <param name="Request">Request parameters</param>
		/// <returns>Information about the registered agent</returns>
		[HttpPost]
		[Route("/api/v1/agents")]
		public async Task<ActionResult<CreateAgentResponse>> CreateAgentAsync([FromBody] CreateAgentRequest Request)
		{
			if(!await AclService.AuthorizeAsync(AclAction.CreateAgent, User))
			{
				return Forbid();
			}

			AgentSoftwareChannelName? Channel = String.IsNullOrEmpty(Request.Channel) ? (AgentSoftwareChannelName?)null : new AgentSoftwareChannelName(Request.Channel);
			IAgent Agent = await AgentService.CreateAgentAsync(Request.Name, Request.Enabled, Request.Ephemeral, Channel, Request.Pools?.ConvertAll(x => new PoolId(x)));

			return new CreateAgentResponse(Agent.Id.ToString());
		}

		/// <summary>
		/// Finds the agents matching specified criteria.
		/// </summary>
		/// <param name="Pool">The pool containing the agent</param>
		/// <param name="Index">First result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <param name="ModifiedAfter">If set, only returns agents modified after this time</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/api/v1/agents")]
		[ProducesResponseType(typeof(List<GetAgentResponse>), 200)]
		public async Task<ActionResult<List<object>>> FindAgentsAsync([FromQuery] string? Pool = null, [FromQuery] int? Index = null, [FromQuery] int? Count = null, [FromQuery] DateTimeOffset? ModifiedAfter = null, [FromQuery] PropertyFilter? Filter = null)
		{
			GlobalPermissionsCache PermissionsCache = new GlobalPermissionsCache();
			List<IAgent> Agents = await AgentService.FindAgentsAsync(Pool?.ToObjectId(), ModifiedAfter?.UtcDateTime, Index, Count);
			List<IPool> Pools = await PoolService.GetPoolsAsync();

			List<object> Responses = new List<object>();
			foreach (IAgent Agent in Agents)
			{
				bool bIncludeAcl = await AgentService.AuthorizeAsync(Agent, AclAction.ViewPermissions, User, PermissionsCache);
				List<PoolId> PoolIds = Agent.GetPools(Pools).Select(x => x.Id).ToList();
				Responses.Add(new GetAgentResponse(Agent, PoolIds, bIncludeAcl).ApplyFilter(Filter));
			}

			return Responses;
		}

		/// <summary>
		/// Retrieve information about a specific agent
		/// </summary>
		/// <param name="AgentId">Id of the agent to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about the requested agent</returns>
		[HttpGet]
		[Route("/api/v1/agents/{AgentId}")]
		[ProducesResponseType(typeof(GetAgentResponse), 200)]
		public async Task<ActionResult<object>> GetAgentAsync(string AgentId, [FromQuery] PropertyFilter? Filter = null)
		{
			IAgent? Agent = await AgentService.GetAgentAsync(new AgentId(AgentId));
			if (Agent == null)
			{
				return NotFound();
			}

			List<IPool> Pools = await PoolService.GetPoolsAsync();

			GlobalPermissionsCache Cache = new GlobalPermissionsCache();
			bool bIncludeAcl = await AgentService.AuthorizeAsync(Agent, AclAction.ViewPermissions, User, Cache);
			List<PoolId> PoolIds = Agent.GetPools(Pools).Select(x => x.Id).ToList();
			return new GetAgentResponse(Agent, PoolIds, bIncludeAcl).ApplyFilter(Filter);
		}

		/// <summary>
		/// Update an agent's properties.
		/// </summary>
		/// <param name="AgentId">Id of the agent to update.</param>
		/// <param name="Update">Properties on the agent to update.</param>
		/// <returns>Http result code</returns>
		[HttpPut]
		[Route("/api/v1/agents/{AgentId}")]
		public async Task<ActionResult> UpdateAgentAsync(string AgentId, [FromBody] UpdateAgentRequest Update)
		{
			IAgent? Agent = await AgentService.GetAgentAsync(new AgentId(AgentId));
			if (Agent == null)
			{
				return NotFound();
			}

			GlobalPermissionsCache Cache = new GlobalPermissionsCache();
			if (!await AgentService.AuthorizeAsync(Agent, AclAction.UpdateAgent, User, Cache))
			{
				return Forbid();
			}
			if (Update.Acl != null && !await AgentService.AuthorizeAsync(Agent, AclAction.ChangePermissions, User, Cache))
			{
				return Forbid();
			}

			AgentSoftwareChannelName? Channel = String.IsNullOrEmpty(Update.Channel) ? (AgentSoftwareChannelName?)null : new AgentSoftwareChannelName(Update.Channel);
			await AgentService.UpdateAgentAsync(Agent, Update.Enabled, Update.RequestConform, Update.RequestRestart, Update.RequestShutdown, Channel, Update.Pools?.ConvertAll(x => new PoolId(x)), Acl.Merge(Agent.Acl, Update.Acl), Update.Comment);
			return Ok();
		}

		/// <summary>
		/// Remove a registered agent.
		/// </summary>
		/// <param name="AgentId">Id of the agent to delete.</param>
		/// <returns>Http result code</returns>
		[HttpDelete]
		[Route("/api/v1/agents/{AgentId}")]
		public async Task<ActionResult> DeleteAgentAsync(string AgentId)
		{
			IAgent? Agent = await AgentService.GetAgentAsync(new AgentId(AgentId));
			if (Agent == null)
			{
				return NotFound();
			}
			if (!await AgentService.AuthorizeAsync(Agent, AclAction.DeleteAgent, User, null))
			{
				return Forbid();
			}

			await AgentService.DeleteAgentAsync(Agent);
			return new OkResult();
		}

		/// <summary>
		/// Find all the sessions of a particular agent
		/// </summary>
		/// <param name="AgentId">Unique id of the agent to find</param>
		/// <param name="StartTime">Start time to include in the search</param>
		/// <param name="FinishTime">Finish time to include in the search</param>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>Sessions </returns>
		[HttpGet]
		[Route("/api/v1/agents/{AgentId}/sessions")]
		public async Task<ActionResult<List<GetAgentSessionResponse>>> FindSessionsAsync(string AgentId, [FromQuery] DateTimeOffset? StartTime, [FromQuery] DateTimeOffset? FinishTime, [FromQuery] int Index = 0, [FromQuery] int Count = 50)
		{
			AgentId AgentIdValue = new AgentId(AgentId);

			IAgent? Agent = await AgentService.GetAgentAsync(AgentIdValue);
			if (Agent == null)
			{
				return NotFound();
			}
			if (!await AgentService.AuthorizeAsync(Agent, AclAction.ViewSession, User, null))
			{
				return Forbid();
			}

			List<ISession> Sessions = await AgentService.FindSessionsAsync(AgentIdValue, StartTime?.UtcDateTime, FinishTime?.UtcDateTime, Index, Count);
			return Sessions.ConvertAll(x => new GetAgentSessionResponse(x));
		}

		/// <summary>
		/// Find all the sessions of a particular agent
		/// </summary>
		/// <param name="AgentId">Unique id of the agent to find</param>
		/// <param name="SessionId">Unique id of the session</param>
		/// <returns>Sessions </returns>
		[HttpGet]
		[Route("/api/v1/agents/{AgentId}/sessions/{SessionId}")]
		public async Task<ActionResult<GetAgentSessionResponse>> GetSessionAsync(string AgentId, string SessionId)
		{
			AgentId AgentIdValue = new AgentId(AgentId);

			IAgent? Agent = await AgentService.GetAgentAsync(AgentIdValue);
			if (Agent == null)
			{
				return NotFound();
			}
			if (!await AgentService.AuthorizeAsync(Agent, AclAction.ViewSession, User, null))
			{
				return Forbid();
			}

			ISession? Session = await AgentService.GetSessionAsync(SessionId.ToObjectId());
			if(Session == null || Session.AgentId != AgentIdValue)
			{
				return NotFound();
			}

			return new GetAgentSessionResponse(Session);
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
		[Route("/api/v1/agents/{AgentId}/leases")]
		public async Task<ActionResult<List<GetAgentLeaseResponse>>> FindLeasesAsync(string AgentId, [FromQuery] string? SessionId, [FromQuery] DateTimeOffset? StartTime, [FromQuery] DateTimeOffset? FinishTime, [FromQuery] int Index = 0, [FromQuery] int Count = 1000)
		{
			AgentId AgentIdValue = new AgentId(AgentId);

			IAgent? Agent = await AgentService.GetAgentAsync(AgentIdValue);
			if (Agent == null)
			{
				return NotFound();
			}
			if (!await AgentService.AuthorizeAsync(Agent, AclAction.ViewLeases, User, null))
			{
				return Forbid();
			}

			List<ILease> Leases = await AgentService.FindLeasesAsync(AgentIdValue, SessionId?.ToObjectId(), StartTime?.UtcDateTime, FinishTime?.UtcDateTime, Index, Count);
			return Leases.ConvertAll(x => new GetAgentLeaseResponse(x));
		}

		/// <summary>
		/// Get info about a particular lease
		/// </summary>
		/// <param name="AgentId">Unique id of the agent to find</param>
		/// <param name="LeaseId">Unique id of the particular lease</param>
		/// <returns>Lease matching the given id</returns>
		[HttpGet]
		[Route("/api/v1/agents/{AgentId}/leases/{LeaseId}")]
		public async Task<ActionResult<GetAgentLeaseResponse>> GetLeaseAsync(string AgentId, string LeaseId)
		{
			AgentId AgentIdValue = new AgentId(AgentId);

			IAgent? Agent = await AgentService.GetAgentAsync(AgentIdValue);
			if(Agent == null)
			{
				return NotFound();
			}
			if (!await AclService.AuthorizeAsync(AclAction.ViewLeases, User))
			{
				return Forbid();
			}

			ObjectId LeaseIdValue = LeaseId.ToObjectId();

			ILease? Lease = await AgentService.GetLeaseAsync(LeaseIdValue);
			if (Lease == null || Lease.AgentId != AgentIdValue)
			{
				return NotFound();
			}

			return new GetAgentLeaseResponse(Lease);
		}
	}
}
