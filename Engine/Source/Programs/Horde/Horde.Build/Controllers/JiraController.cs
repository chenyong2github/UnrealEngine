// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using Horde.Build.Acls;
using Horde.Build.Api;
using Horde.Build.Models;
using Horde.Build.Services;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;

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
		readonly JiraService _jiraService;

		/// <summary>
		/// Singleton instance of the stream service
		/// </summary>
		readonly StreamService _streamService;

		/// <summary>
		/// Constructor
		/// </summary>
		public JiraController(JiraService jiraService, StreamService streamService)
		{
			_jiraService = jiraService;
			_streamService = streamService;
		}

		/// <summary>
		/// Get jira issue information for provided keys
		/// </summary>
		[HttpGet]
		[Authorize]
		[Route("/api/v1/jira")]
		public async Task<ActionResult<List<GetJiraIssueResponse>>> GetJiraIssuesAsync([FromQuery] string streamId, [FromQuery] string[] jiraKeys)
		{

			StreamId streamIdValue = new StreamId(streamId);
			StreamPermissionsCache cache = new StreamPermissionsCache();

			if (!await _streamService.AuthorizeAsync(streamIdValue, AclAction.ViewStream, User, cache))
			{
				return Forbid();
			}

			List<GetJiraIssueResponse> response = new List<GetJiraIssueResponse>();

			if (jiraKeys.Length != 0)
			{
				List<JiraIssue> issues = await _jiraService.GetJiraIssuesAsync(jiraKeys);

				for (int i = 0; i < issues.Count; i++)
				{
					response.Add(new GetJiraIssueResponse(issues[i]));
				}
			}			

			return response;
		}
	}
}
