// Copyright Epic Games, Inc. All Rights Reserved.	

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Polly;
using Polly.Extensions.Http;

namespace Horde.Build.Services
{
	/// <summary>
	/// Jira service functinality
	/// </summary>
	public sealed class JiraService : IJiraService, IDisposable
	{
		readonly ILogger _logger;

		/// <summary>
		/// The server settings
		/// </summary>
		readonly ServerSettings _settings;
		readonly HttpClient _client;
		readonly AsyncPolicy<HttpResponseMessage> _retryPolicy;
		readonly ConcurrentDictionary<string, JiraCacheValue> _issueCache = new ConcurrentDictionary<string, JiraCacheValue>();

		/// <summary>
		/// Jira service constructor
		/// </summary>
		/// <param name="settings"></param>
		/// <param name="logger"></param>
		public JiraService(IOptions<ServerSettings> settings, ILogger<JiraService> logger)
		{

			_settings = settings.Value;
			_logger = logger;

			// setup http client for Jira rest api queries
			_client = new HttpClient();
			byte[] authBytes = Encoding.ASCII.GetBytes($"{_settings.JiraUsername}:{_settings.JiraApiToken}");
			_client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Basic", Convert.ToBase64String(authBytes));
			_client.Timeout = TimeSpan.FromSeconds(15.0);			
			_retryPolicy = HttpPolicyExtensions.HandleTransientHttpError().WaitAndRetryAsync(3, attempt => TimeSpan.FromSeconds(Math.Pow(2.0, attempt)));
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_client.Dispose();
		}

		/// <inheritdoc/>
		public async Task<List<JiraIssue>> GetJiraIssuesAsync(string[] jiraKeys)
		{
			List<JiraIssue> result = new List<JiraIssue>();
			
			if (_settings.JiraUrl == null || jiraKeys.Length == 0)
			{
				return result;
			}

			List<string> queryJiras = new List<string>();
			for (int i = 0; i < jiraKeys.Length; i++)
			{
				JiraCacheValue value;
				if (_issueCache.TryGetValue(jiraKeys[i], out value))
				{
					if (DateTime.UtcNow.Subtract(value._cacheTime).TotalMinutes >= 2)
					{
						queryJiras.Add(jiraKeys[i]);				
					}
				}
				else
				{
					queryJiras.Add(jiraKeys[i]);
				}
			}

			if (queryJiras.Count > 0)
			{
				Uri uri = new Uri(_settings.JiraUrl, $"/rest/api/2/search?jql=issueKey%20in%20({String.Join(",", queryJiras)})&fields=assignee,status,resolution,priority&maxResults={queryJiras.Count}");

				HttpResponseMessage response = await _retryPolicy.ExecuteAsync(() => _client.GetAsync(uri));
				if (!response.IsSuccessStatusCode)
				{
					_logger.LogError("GET to {Uri} returned {Code} ({Response})", uri, response.StatusCode, await response.Content.ReadAsStringAsync());
					throw new Exception("GetJiraIssuesAsync call failed");
				}

				byte[] data = await response.Content.ReadAsByteArrayAsync();
				IssueQueryResponse? jiras = JsonSerializer.Deserialize<IssueQueryResponse>(data.AsSpan(), new JsonSerializerOptions() { PropertyNameCaseInsensitive = true });

				if (jiras != null)
				{
					for (int i = 0; i < jiras.Issues.Count; i++)
					{
						IssueResponse issue = jiras.Issues[i];

						JiraIssue jiraIssue = new JiraIssue();
						jiraIssue.Key = issue.Key;
						jiraIssue.JiraLink = $"{_settings.JiraUrl}browse/{issue.Key}";
						jiraIssue.AssigneeName = issue.Fields?.Assignee?.Name;
						jiraIssue.AssigneeDisplayName = issue.Fields?.Assignee?.DisplayName;
						jiraIssue.AssigneeEmailAddress = issue.Fields?.Assignee?.EmailAddress;
						jiraIssue.PriorityName = issue.Fields?.Priority?.Name;
						jiraIssue.ResolutionName = issue.Fields?.Resolution?.Name;
						jiraIssue.StatusName = issue.Fields?.Status?.Name;

						_issueCache[issue.Key] = new JiraCacheValue() { _issue = jiraIssue, _cacheTime = DateTime.UtcNow };
					}
				}
			}

			for (int i = 0; i < jiraKeys.Length; i++)
			{
				JiraCacheValue value;
				if (_issueCache.TryGetValue(jiraKeys[i], out value))
				{
					result.Add(value._issue);
				}
			}

			return result;

		}

		// Jira rest api mapping and caching

		struct JiraCacheValue
		{
			public JiraIssue _issue;
			public DateTime _cacheTime;
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
