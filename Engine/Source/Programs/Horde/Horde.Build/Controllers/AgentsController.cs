// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	using AgentSoftwareChannelName = StringId<AgentSoftwareChannels>;
	using LeaseId = ObjectId<ILease>;
	using PoolId = StringId<IPool>;

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
			IAgent Agent = await AgentService.CreateAgentAsync(Request.Name, Request.Enabled, Channel, Request.Pools?.ConvertAll(x => new PoolId(x)));

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

			List<object> Responses = new List<object>();
			foreach (IAgent Agent in Agents)
			{
				bool bIncludeAcl = await AgentService.AuthorizeAsync(Agent, AclAction.ViewPermissions, User, PermissionsCache);
				Responses.Add(new GetAgentResponse(Agent, bIncludeAcl).ApplyFilter(Filter));
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

			GlobalPermissionsCache Cache = new GlobalPermissionsCache();
			bool bIncludeAcl = await AgentService.AuthorizeAsync(Agent, AclAction.ViewPermissions, User, Cache);
			return new GetAgentResponse(Agent, bIncludeAcl).ApplyFilter(Filter);
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
			List<PoolId>? UpdatePools = Update.Pools?.ConvertAll(x => new PoolId(x));
			AgentSoftwareChannelName? Channel = String.IsNullOrEmpty(Update.Channel) ? (AgentSoftwareChannelName?)null : new AgentSoftwareChannelName(Update.Channel);

			string UserName = User.GetUserName() ?? "Unknown";

			GlobalPermissionsCache Cache = new GlobalPermissionsCache();
			for (; ; )
			{
				IAgent? Agent = await AgentService.GetAgentAsync(new AgentId(AgentId));
				if (Agent == null)
				{
					return NotFound();
				}
				if (!await AgentService.AuthorizeAsync(Agent, AclAction.UpdateAgent, User, Cache))
				{
					return Forbid();
				}
				if (Update.Acl != null && !await AgentService.AuthorizeAsync(Agent, AclAction.ChangePermissions, User, Cache))
				{
					return Forbid();
				}

				IAgent? NewAgent = await AgentService.Agents.TryUpdateSettingsAsync(Agent, Update.Enabled, Update.RequestConform, Update.RequestRestart, Update.RequestShutdown, Channel, Update.Pools?.ConvertAll(x => new PoolId(x)), Acl.Merge(Agent.Acl, Update.Acl), Update.Comment);
				if (NewAgent == null)
				{
					continue;
				}

				IAuditLogChannel<AgentId> Logger = AgentService.Agents.GetLogger(Agent.Id);
				if (Agent.Enabled != NewAgent.Enabled)
				{
					Logger.LogInformation("Setting changed: Enabled = {State} ({UserName})", NewAgent.Enabled, UserName);
				}
				if (Agent.RequestConform != NewAgent.RequestConform)
				{
					Logger.LogInformation("Setting changed: RequestConfig = {State} ({UserName})", NewAgent.RequestConform, UserName);
				}
				if (Agent.RequestRestart != NewAgent.RequestRestart)
				{
					Logger.LogInformation("Setting changed: RequestRestart = {State} ({UserName})", NewAgent.RequestRestart, UserName);
				}
				if (Agent.RequestShutdown != NewAgent.RequestShutdown)
				{
					Logger.LogInformation("Setting changed: RequestShutdown = {State} ({UserName})", NewAgent.RequestShutdown, UserName);
				}
				if (Agent.Comment != NewAgent.Comment)
				{
					Logger.LogInformation("Setting changed: Comment = \"{Comment}\" ({UserName})", Update.Comment, UserName);
				}
				foreach (PoolId AddedPool in NewAgent.ExplicitPools.Except(Agent.ExplicitPools))
				{
					Logger.LogInformation("Added to pool {PoolId} ({UserName})", AddedPool, UserName);
				}
				foreach (PoolId RemovedPool in Agent.ExplicitPools.Except(NewAgent.ExplicitPools))
				{
					Logger.LogInformation("Removed from pool {PoolId} ({UserName})", RemovedPool, UserName);
				}
				break;
			}
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
		/// Retrieve historical information about a specific agent
		/// </summary>
		/// <param name="AgentId">Id of the agent to get information about</param>
		/// <param name="MinTime">Minimum time for records to return</param>
		/// <param name="MaxTime">Maximum time for records to return</param>
		/// <param name="Index">Offset of the first result</param>
		/// <param name="Count">Number of records to return</param>
		/// <returns>Information about the requested agent</returns>
		[HttpGet]
		[Route("/api/v1/agents/{AgentId}/history")]
		public async Task GetAgentHistoryAsync(string AgentId, [FromQuery] DateTime? MinTime = null, [FromQuery] DateTime? MaxTime = null, [FromQuery] int Index = 0, [FromQuery] int Count = 50)
		{
			Response.ContentType = "application/json";
			Response.StatusCode = 200;
			await Response.StartAsync();
			await AgentService.Agents.GetLogger(AgentId.ToAgentId()).FindAsync(Response.BodyWriter, MinTime, MaxTime, Index, Count);
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
		public async Task<ActionResult<GetAgentLeaseResponse>> GetLeaseAsync(AgentId AgentId, LeaseId LeaseId)
		{
			IAgent? Agent = await AgentService.GetAgentAsync(AgentId);
			if(Agent == null)
			{
				return NotFound();
			}
			if (!await AclService.AuthorizeAsync(AclAction.ViewLeases, User))
			{
				return Forbid();
			}

			ILease? Lease = await AgentService.GetLeaseAsync(LeaseId);
			if (Lease == null || Lease.AgentId != AgentId)
			{
				return NotFound();
			}

			return new GetAgentLeaseResponse(Lease);
		}
	}
}
