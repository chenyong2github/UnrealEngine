// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using HordeServer.Models;
using HordeServer.Services;

namespace HordeServer.Api
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
		public GetJiraIssueResponse(JiraIssue Issue)
		{
			this.Key = Issue.Key;
			this.JiraLink = Issue.JiraLink;
			this.StatusName = Issue.StatusName;
			this.ResolutionName = Issue.ResolutionName;
			this.PriorityName = Issue.PriorityName;
			this.AssigneeName = Issue.AssigneeName;
			this.AssigneeDisplayName = Issue.AssigneeDisplayName;
			this.AssigneeEmailAddress = Issue.AssigneeEmailAddress;
		}

	}

}

 