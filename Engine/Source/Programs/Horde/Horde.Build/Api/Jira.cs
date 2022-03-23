// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Build.Services;

namespace Horde.Build.Api
{

	/// <summary>
	/// Jira issue response object
	/// </summary>
	public class GetJiraIssueResponse
	{
		/// <summary>
		/// The Jira issue key
		/// </summary>
		public string Key { get; set; }

		/// <summary>
		/// The Jira issue link
		/// </summary>
		public string JiraLink { get; set; }

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

		/// <summary>
		/// Response constructor
		/// </summary>
		public GetJiraIssueResponse(JiraIssue issue)
		{
			Key = issue.Key;
			JiraLink = issue.JiraLink;
			StatusName = issue.StatusName;
			ResolutionName = issue.ResolutionName;
			PriorityName = issue.PriorityName;
			AssigneeName = issue.AssigneeName;
			AssigneeDisplayName = issue.AssigneeDisplayName;
			AssigneeEmailAddress = issue.AssigneeEmailAddress;
		}
	}
}

 