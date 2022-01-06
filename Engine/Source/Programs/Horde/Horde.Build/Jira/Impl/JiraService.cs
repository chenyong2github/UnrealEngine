// Copyright Epic Games, Inc. All Rights Reserved.	

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using System.Net.Http;
using Polly;
using Polly.Extensions.Http;
using System.Text.Json;
using System.Net.Http.Headers;
using System.Collections.Concurrent;

namespace HordeServer.Services
{
	/// <summary>
	/// Jira service functinality
	/// </summary>
	public sealed class JiraService : IJiraService, IDisposable
	{
		ILogger Logger;

		/// <summary>
		/// The server settings
		/// </summary>
		ServerSettings Settings;

		HttpClient Client;
		AsyncPolicy<HttpResponseMessage> RetryPolicy;

		ConcurrentDictionary<string, JiraCacheValue> IssueCache = new ConcurrentDictionary<string, JiraCacheValue>();

		/// <summary>
		/// Jira service constructor
		/// </summary>
		/// <param name="Settings"></param>
		/// <param name="Logger"></param>
		public JiraService(IOptions<ServerSettings> Settings, ILogger<JiraService> Logger)
		{

			this.Settings = Settings.Value;
			this.Logger = Logger;

			// setup http client for Jira rest api queries
			Client = new HttpClient();
			byte[] AuthBytes = Encoding.ASCII.GetBytes($"{this.Settings.JiraUsername}:{this.Settings.JiraApiToken}");
			Client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Basic", Convert.ToBase64String(AuthBytes));
			Client.Timeout = TimeSpan.FromSeconds(15.0);			
			RetryPolicy = HttpPolicyExtensions.HandleTransientHttpError().WaitAndRetryAsync(3, Attempt => TimeSpan.FromSeconds(Math.Pow(2.0, Attempt)));
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Client.Dispose();
		}

		/// <inheritdoc/>
		public async Task<List<JiraIssue>> GetJiraIssuesAsync(string[] JiraKeys)
		{
			List<JiraIssue> Result = new List<JiraIssue>();
			
			if (Settings.JiraUrl == null || JiraKeys.Length == 0)
			{
				return Result;
			}

			List<string> QueryJiras = new List<string>();
			for (int i = 0; i < JiraKeys.Length; i++)
			{
				JiraCacheValue Value;
				if (IssueCache.TryGetValue(JiraKeys[i], out Value))
				{
					if (DateTime.UtcNow.Subtract(Value.CacheTime).TotalMinutes >= 2)
					{
						QueryJiras.Add(JiraKeys[i]);				
					}
				}
				else
				{
					QueryJiras.Add(JiraKeys[i]);
				}
			}

			if (QueryJiras.Count > 0)
			{
				Uri Uri = new Uri(Settings.JiraUrl, $"/rest/api/2/search?jql=issueKey%20in%20({string.Join(",", QueryJiras)})&fields=assignee,status,resolution,priority&maxResults={QueryJiras.Count}");

				HttpResponseMessage Response = await RetryPolicy.ExecuteAsync(() => Client.GetAsync(Uri));
				if (!Response.IsSuccessStatusCode)
				{
					Logger.LogError("GET to {Uri} returned {Code} ({Response})", Uri, Response.StatusCode, await Response.Content.ReadAsStringAsync());
					throw new Exception("GetJiraIssuesAsync call failed");
				}

				byte[] Data = await Response.Content.ReadAsByteArrayAsync();
				IssueQueryResponse? Jiras = JsonSerializer.Deserialize<IssueQueryResponse>(Data.AsSpan(), new JsonSerializerOptions() { PropertyNameCaseInsensitive = true });

				if (Jiras != null)
				{
					for (int i = 0; i < Jiras.Issues.Count; i++)
					{
						IssueResponse Issue = Jiras.Issues[i];

						JiraIssue JiraIssue = new JiraIssue();
						JiraIssue.Key = Issue.Key;
						JiraIssue.JiraLink = $"{Settings.JiraUrl}browse/{Issue.Key}";
						JiraIssue.AssigneeName = Issue.Fields?.Assignee?.Name;
						JiraIssue.AssigneeDisplayName = Issue.Fields?.Assignee?.DisplayName;
						JiraIssue.AssigneeEmailAddress = Issue.Fields?.Assignee?.EmailAddress;
						JiraIssue.PriorityName = Issue.Fields?.Priority?.Name;
						JiraIssue.ResolutionName = Issue.Fields?.Resolution?.Name;
						JiraIssue.StatusName = Issue.Fields?.Status?.Name;

						IssueCache[Issue.Key] = new JiraCacheValue() { Issue = JiraIssue, CacheTime = DateTime.UtcNow };
					}
				}
			}

			for (int i = 0; i < JiraKeys.Length; i++)
			{
				JiraCacheValue Value;
				if (IssueCache.TryGetValue(JiraKeys[i], out Value))
				{
					Result.Add(Value.Issue);
				}
			}

			return Result;

		}

		// Jira rest api mapping and caching

		struct JiraCacheValue
		{
			public JiraIssue Issue;
			public DateTime CacheTime;
		};

		class IssueQueryResponse
		{
			public int Total { get; set; } = 0;

			public List<IssueResponse> Issues { get; set; } = new List<IssueResponse>();
		}

		class IssueResponse
		{
			public string Key { get; set; } = String.Empty;

			public IssueFields? Fields { get; set; }
		}

		class IssueFields
		{
			public AssigneeField? Assignee { get; set; }

			public StatusField? Status { get; set; }

			public ResolutionField? Resolution { get; set; }

			public PriorityField? Priority { get; set; }
		}

		class AssigneeField
		{
			public string? Name { get; set; }

			public string? DisplayName { get; set; }

			public string? EmailAddress { get; set; }
		}

		class StatusField
		{
			public string? Name { get; set; }
		}

		class PriorityField
		{
			public string? Name { get; set; }
		}

		class ResolutionField
		{
			public string? Name { get; set; }
		}

	}
}
