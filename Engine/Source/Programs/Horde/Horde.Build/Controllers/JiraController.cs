// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using System.Threading.Tasks;
using HordeServer.Services;
using Microsoft.Extensions.Logging;
using HordeServer.Api;
using System.Collections.Generic;
using HordeServer.Models;
using HordeServer.Utilities;

namespace Horde.Build
{
	using StreamId = StringId<IStream>;

	/// <summary>	
	/// Jira service controller
	/// </summary>	
	[ApiController]
	[Route("[controller]")]
	public class JiraController : Controller
	{
		/// <summary>
		/// Singleton instance of the jira service
		/// </summary>
		JiraService JiraService;

		/// <summary>
		/// Singleton instance of the stream service
		/// </summary>
		StreamService StreamService;

		/// <summary>
		///  Logger for controller
		/// </summary>
		private readonly ILogger<JiraController> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public JiraController(JiraService JiraService, StreamService StreamService, ILogger<JiraController> Logger)
		{
			this.JiraService = JiraService;
			this.Logger = Logger;
			this.StreamService = StreamService;
		}

		/// <summary>
		/// Get jira issue information for provided keys
		/// </summary>
		[HttpGet]
		[Authorize]
		[Route("/api/v1/jira")]
		public async Task<ActionResult<List<GetJiraIssueResponse>>> GetJiraIssuesAsync([FromQuery] string StreamId, [FromQuery] string[] JiraKeys)
		{

			StreamId StreamIdValue = new StreamId(StreamId);
			StreamPermissionsCache Cache = new StreamPermissionsCache();

			if (!await StreamService.AuthorizeAsync(StreamIdValue, AclAction.ViewStream, User, Cache))
			{
				return Forbid();
			}

			List<GetJiraIssueResponse> Response = new List<GetJiraIssueResponse>();

			if (JiraKeys.Length != 0)
			{
				List<JiraIssue> Issues = await this.JiraService.GetJiraIssuesAsync(JiraKeys);

				for (int i = 0; i < Issues.Count; i++)
				{
					Response.Add(new GetJiraIssueResponse(Issues[i]));
				}

			}			

			return Response;
		}


	}
}
