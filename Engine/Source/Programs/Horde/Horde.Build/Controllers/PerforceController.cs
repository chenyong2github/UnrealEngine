// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Mvc;
using System.Net;
using Microsoft.Extensions.Logging;
using System.Threading.Tasks;
using System.Collections.Generic;
using HordeServer.Services;
using Microsoft.AspNetCore.Authorization;
using HordeServer.Models;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for Perforce triggers and callbacks
	/// </summary>
	[Authorize]
	[ApiController]
	[Route("[controller]")]
	public class PerforceController : ControllerBase
	{
		/// <summary>
		/// The database service instance
		/// </summary>
		private DatabaseService DatabaseService;

		/// <summary>
		/// The ACL service instance
		/// </summary>
		private AclService AclService;

		/// <summary>
		/// Load balancer instance
		/// </summary>
		private PerforceLoadBalancer PerforceLoadBalancer;

		/// <summary>
		/// Constructor
		/// </summary>
		public PerforceController(DatabaseService DatabaseService, AclService AclService, PerforceLoadBalancer PerforceLoadBalancer)
		{
			this.DatabaseService = DatabaseService;
			this.AclService = AclService;
			this.PerforceLoadBalancer = PerforceLoadBalancer;
		}

		/// <summary>
		/// Get the current server status
		/// </summary>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v1/perforce/status")]
		public async Task<ActionResult<List<object>>> GetStatusAsync()
		{
			List<IPerforceServer> Servers = await PerforceLoadBalancer.GetServersAsync();

			List<object> Responses = new List<object>();
			foreach (IPerforceServer Server in Servers)
			{
				Responses.Add(new { Server.ServerAndPort, Server.BaseServerAndPort, Server.Cluster, Server.NumLeases, Server.Status, Server.Detail, Server.LastUpdateTime });
			}
			return Responses;
		}

		/// <summary>
		/// Gets the current perforce settinsg
		/// </summary>
		/// <returns>List of Perforce clusters</returns>
		[HttpGet]
		[Route("/api/v1/perforce/settings")]
		public async Task<ActionResult<List<PerforceCluster>>> GetPerforceSettingsAsync()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			Globals Globals = await DatabaseService.GetGlobalsAsync();
			return Globals.PerforceClusters;
		}
	}

	/// <summary>
	/// Controller for Perforce triggers and callbacks
	/// </summary>
	[ApiController]
	[Route("[controller]")]
	public class PublicPerforceController : ControllerBase
	{
		/// <summary>
		/// Logger instance
		/// </summary>
		private readonly ILogger<PerforceController> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public PublicPerforceController(ILogger<PerforceController> Logger)
		{
			this.Logger = Logger;
		}

		/// <summary>
		/// Endpoint which trigger scripts in Perforce will call
		/// </summary>
		/// <returns>200 OK on success</returns>
		[HttpPost]
		[Route("/api/v1/perforce/trigger/{Type}")]
		public ActionResult TriggerCallback(string Type, [FromQuery] long? Changelist = null, [FromQuery(Name = "user")] string? PerforceUser = null)
		{
			// Currently just a placeholder until correct triggers are in place.
			Logger.LogDebug("Received Perforce trigger callback. Type={Type} Changelist={Changelist} User={User}", Type, Changelist, PerforceUser);
			string Content = "{\"message\": \"Trigger received\"}";
			return new ContentResult { ContentType = "text/plain", StatusCode = (int)HttpStatusCode.OK, Content = Content };
		}
	}
}
