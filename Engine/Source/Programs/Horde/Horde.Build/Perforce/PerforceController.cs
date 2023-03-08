// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net;
using System.Threading.Tasks;
using Horde.Build.Acls;
using Horde.Build.Server;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Build.Perforce
{
	/// <summary>
	/// Controller for Perforce triggers and callbacks
	/// </summary>
	[Authorize]
	[ApiController]
	[Route("[controller]")]
	public class PerforceController : ControllerBase
	{
		private readonly PerforceLoadBalancer _perforceLoadBalancer;
		private readonly IOptionsSnapshot<GlobalConfig> _globalConfig;

		/// <summary>
		/// Constructor
		/// </summary>
		public PerforceController(PerforceLoadBalancer perforceLoadBalancer, IOptionsSnapshot<GlobalConfig> globalConfig)
		{
			_perforceLoadBalancer = perforceLoadBalancer;
			_globalConfig = globalConfig;
		}

		/// <summary>
		/// Get the current server status
		/// </summary>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v1/perforce/status")]
		public async Task<ActionResult<List<object>>> GetStatusAsync()
		{
			List<IPerforceServer> servers = await _perforceLoadBalancer.GetServersAsync();

			List<object> responses = new List<object>();
			foreach (IPerforceServer server in servers)
			{
				responses.Add(new { server.ServerAndPort, server.BaseServerAndPort, server.Cluster, server.NumLeases, server.Status, server.Detail, server.LastUpdateTime });
			}
			return responses;
		}

		/// <summary>
		/// Gets the current perforce settinsg
		/// </summary>
		/// <returns>List of Perforce clusters</returns>
		[HttpGet]
		[Route("/api/v1/perforce/settings")]
		public ActionResult<List<PerforceCluster>> GetPerforceSettingsAsync()
		{
			GlobalConfig globalConfig = _globalConfig.Value;

			if (!globalConfig.Authorize(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			return globalConfig.PerforceClusters;
		}
	}

	/// <summary>
	/// Perforce trigger received as JSON
	/// </summary>
	public class PerforceTriggerPayload
	{
		/// <summary>
		/// Type of trigger (change-commit etc)
		/// </summary>
		public string TriggerType { get; }
		
		/// <summary>
		/// Author of changelist (Perforce user) 
		/// </summary>
		public string User { get; }
		
		/// <summary>
		/// Change number
		/// </summary>
		public string Changelist { get; }
		
		/// <summary>
		/// Change number
		/// </summary>
		public int ChangelistNumber { get; }
		
		/// <summary>
		/// Change root of changelist
		/// </summary>
		public string ChangeRoot { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="triggerType"></param>
		/// <param name="user"></param>
		/// <param name="changelist"></param>
		/// <param name="changeRoot"></param>
		public PerforceTriggerPayload(string triggerType, string user, string changelist, string changeRoot)
		{
			TriggerType = triggerType;
			User = user;
			Changelist = changelist;
			ChangelistNumber = Convert.ToInt32(changelist);
			ChangeRoot = changeRoot;
		}

		/// <summary>
		/// Format as a string for debugging purposes
		/// </summary>
		/// <returns></returns>
		public override string ToString()
		{
			return $"TriggerType={TriggerType} User={User} CL={Changelist} Root={ChangeRoot}";
		}
	}
	
	/// <summary>
	/// Controller for Perforce triggers and callbacks
	/// </summary>
	[ApiController]
	[Route("[controller]")]
	public class PublicPerforceController : ControllerBase
	{
		private readonly IPerforceService _perforceService;
		private readonly ILogger<PerforceController> _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public PublicPerforceController(IPerforceService perforceService, ILogger<PerforceController> logger)
		{
			_perforceService = perforceService;
			_logger = logger;
		}

		/// <summary>
		/// Endpoint which trigger scripts in Perforce will call
		/// </summary>
		/// <returns>200 OK on success</returns>
		[HttpPost]
		[Route("/api/v1/perforce/trigger")]
		public ActionResult TriggerCallback([FromBody]PerforceTriggerPayload payload)
		{
			// Currently just a placeholder until correct triggers are in place.
			_logger.LogDebug("Received Perforce trigger callback. Type={Type} CL={Changelist} User={User} Root={Root}", 
				payload.TriggerType, payload.Changelist, payload.User, payload.ChangeRoot);

			string content = "{\"message\": \"Trigger received\"}";
			return new ContentResult { ContentType = "application/json", StatusCode = (int)HttpStatusCode.OK, Content = content };
		}
	}
}
