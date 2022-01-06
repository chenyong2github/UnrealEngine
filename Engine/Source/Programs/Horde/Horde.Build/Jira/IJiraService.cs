// Copyright Epic Games, Inc. All Rights Reserved.	

using System.Collections.Generic;
using System.Threading.Tasks;

namespace HordeServer.Services
{

	/// <summary>
	/// Jira issue information
	/// </summary>
	public class JiraIssue
	{
		/// <summary>
		/// The Jira issue key
		/// </summary>
		public string Key { get; set; } = string.Empty;

		/// <summary>
		/// The Jira issue link
		/// </summary>
		public string JiraLink { get; set; } = string.Empty;

		/// <summary>
		/// The issue status name, "To Do", "In Progress", etc
		/// </summary>
		public string? StatusName { get; set; }

		/// <summary>
		/// The issue resolution name, "Fixed", "Closed", etc
		/// </summary>
		public string? ResolutionName { get; set; }

		/// <summary>
		/// The issue priority name, "1 - Critical", "2 - Major", etc
		/// </summary>
		public string? PriorityName { get; set; }

		/// <summary>
		/// The current assignee's user name
		/// </summary>
		public string? AssigneeName { get; set; }

		/// <summary>
		/// The current assignee's display name
		/// </summary>
		public string? AssigneeDisplayName { get; set; }

		/// <summary>
		/// The current assignee's email address
		/// </summary>
		public string? AssigneeEmailAddress { get; set; }
	}

	/// <summary>
	/// Wrapper around Jira functionality.
	/// </summary>
	public interface IJiraService
	{
		/// <summary>
		/// Get Jira issues associated with provided keys
		/// </summary>
		/// <param name="JiraKeys"></param>
		/// <returns></returns>
		Task<List<JiraIssue>> GetJiraIssuesAsync(string[] JiraKeys);
	}

}