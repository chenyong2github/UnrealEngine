// Copyright Epic Games, Inc. All Rights Reserved.	

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Horde.Build.Models;
using Horde.Build.Utilities;
using Microsoft.Extensions.Hosting;

namespace Horde.Build.Services
{
	using StreamId = StringId<IStream>;

	/// <summary>
	/// External issue tracking project
	/// </summary>
	public interface IExternalIssueProject
	{
		/// <summary>
		/// Project key
		/// </summary>
		string Key { get; }

		/// <summary>
		/// Project name
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Project id
		/// </summary>
		string Id { get; }

		/// <summary>
		/// component id => name
		/// </summary>
		Dictionary<string, string> Components { get; }

		/// <summary>
		/// // IssueType id => name
		/// </summary>
		Dictionary<string, string> IssueTypes { get; }
	}

	/// <summary>
	/// Interface for an external issue tracking service
	/// </summary>
	public interface IExternalIssueService : IHostedService
	{
		/// <summary>
		/// Get issues associated with provided keys
		/// </summary>
		/// <param name="keys"></param>
		/// <returns></returns>
		Task<List<IExternalIssue>> GetIssuesAsync(string[] keys);

		/// <summary>
		/// Create and link an external issue
		/// </summary>		
		/// <param name="User"></param>
		/// <param name="issueId"></param>
		/// <param name="summary"></param>
		/// <param name="projectId"></param>
		/// <param name="componentId"></param>
		/// <param name="issueType"></param>
		/// <param name="description"></param>
		/// <param name="hordeIssueLink"></param>
		Task<(string? key, string? url)> CreateIssueAsync(IUser User, int issueId, string summary, string projectId, string componentId, string issueType, string? description, string? hordeIssueLink);


		/// <summary>
		///  Get projects for provided keys
		/// </summary>
		/// <param name="stream"></param>
		/// <returns></returns>;
		Task<List<IExternalIssueProject>> GetProjects(IStream stream);

	}

	/// <summary>
	/// Default external issue service
	/// </summary>
	public class DefaultExternalIssueService : IExternalIssueService
	{
		/// <inheritdoc/>
		public Task<List<IExternalIssue>> GetIssuesAsync(string[] keys)
		{
			return Task.FromResult(new List<IExternalIssue>());
		}

		/// <inheritdoc/>
		public Task<(string? key, string? url)> CreateIssueAsync(IUser User, int issueId, string summary, string projectId, string componentId, string issueType, string? description, string? hordeIssueLink)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public Task<List<IExternalIssueProject>> GetProjects(IStream stream)
		{
			return Task.FromResult(new List<IExternalIssueProject>());
		}

		/// <inheritdoc/>
		public void Dispose() { }

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken token) => Task.CompletedTask;

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken token) => Task.CompletedTask;
	}

}