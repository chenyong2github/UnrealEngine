// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics;
using System.Reflection;
using System.Threading.Tasks;
using Horde.Server.Agents;
using Horde.Server.Tools;
using HordeCommon;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;

namespace Horde.Server.Server
{
	/// <summary>
	/// Controller managing account status
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ServerController : ControllerBase
	{
		readonly IToolCollection _toolCollection;
		readonly IClock _clock;
		readonly IOptionsSnapshot<GlobalConfig> _globalConfig;

		/// <summary>
		/// Constructor
		/// </summary>
		public ServerController(IToolCollection toolCollection, IClock clock, IOptionsSnapshot<GlobalConfig> globalConfig)
		{
			_toolCollection = toolCollection;
			_clock = clock;
			_globalConfig = globalConfig;
		}
		
		/// <summary>
		/// Get server version
		/// </summary>
		[HttpGet]
		[Route("/api/v1/server/version")]
		public ActionResult GetVersionAsync()
		{
			FileVersionInfo fileVersionInfo = FileVersionInfo.GetVersionInfo(Assembly.GetExecutingAssembly().Location);
			return Ok(fileVersionInfo.ProductVersion);
		}		

		/// <summary>
		/// Get server information
		/// </summary>
		[HttpGet]
		[Route("/api/v1/server/info")]
		[ProducesResponseType(typeof(GetServerInfoResponse), 200)]
		public async Task<ActionResult<GetServerInfoResponse>> GetServerInfo()
		{
			string? agentVersion = null;

			ITool? tool = await _toolCollection.GetAsync(AgentExtensions.DefaultAgentSoftwareToolId, _globalConfig.Value);
			if (tool != null)
			{
				IToolDeployment? deployment = tool.GetCurrentDeployment(1.0, _clock.UtcNow);
				if (deployment != null)
				{
					agentVersion = deployment.Version;
				}
			}

			return new GetServerInfoResponse(agentVersion);
		}
	}
}
