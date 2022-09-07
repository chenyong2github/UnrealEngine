// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Perforce;
using Horde.Build.Jobs.Templates;
using Horde.Build.Streams;
using Horde.Build.Users;

namespace Horde.Build.Perforce
{
	/// <summary>
	/// Result from checking a shelved change status
	/// </summary>
	public enum CheckShelfResult
	{
		/// <summary>
		/// The shelf is ok
		/// </summary>
		Ok,

		/// <summary>
		/// Changelist does not exist
		/// </summary>
		NoChange,

		/// <summary>
		/// The change does not contain any shelved files
		/// </summary>
		NoShelvedFiles,

		/// <summary>
		/// The shelf contains *some* files from a different stream
		/// </summary>
		MixedStream,

		/// <summary>
		/// The shelf contains only files from a different stream
		/// </summary>
		WrongStream,
	}

	/// <summary>
	/// Wrapper around Perforce functionality. Can use a local p4.exe client for development purposes, or a separate HordePerforceBridge instance over REST for deployments.
	/// </summary>
	public interface IPerforceService
	{
		/// <summary>
		/// Connect to the given Perforce cluster
		/// </summary>
		/// <param name="clusterName">Name of the cluster</param>
		/// <param name="userName">Name of the user to connect as. Uses the service account if null.</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Connection information</returns>
		public Task<IPerforceConnection> ConnectAsync(string clusterName, string? userName = null, CancellationToken cancellationToken = default);

		/// <summary>
		/// Finds or adds a user from the given Perforce server, adding the user (and populating their profile with Perforce data) if they do not currently exist
		/// </summary>
		/// <param name="clusterName"></param>
		/// <param name="userName"></param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public ValueTask<IUser> FindOrAddUserAsync(string clusterName, string userName, CancellationToken cancellationToken = default);

		/// <summary>
		/// Create a new changelist by submitting the given file
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="streamName">The stream to query</param>
		/// <param name="path">Path for the file to submit</param>
		/// <param name="description">Description for the changelist</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New changelist number</returns>
		public Task<int> CreateNewChangeAsync(string clusterName, string streamName, string path, string description, CancellationToken cancellationToken = default);

		/// <summary>
		/// Gets the code change corresponding to an actual change submitted to a stream
		/// </summary>
		/// <param name="stream">Stream to query</param>
		/// <param name="change">The changelist number to query</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Code change for the latest change</returns>
		public Task<int> GetCodeChangeAsync(IStream stream, int change, CancellationToken cancellationToken = default);

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="stream">Stream to query</param>
		/// <param name="minChange">The minimum changelist number</param>
		/// <param name="maxChange">The maximum changelist number</param>
		/// <param name="results">Number of results to return</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Latest changelist number</returns>
		public Task<List<ICommit>> GetChangesAsync(IStream stream, int? minChange, int? maxChange, int? results, CancellationToken cancellationToken = default);

		/// <summary>
		/// Checks a shelf is valid for the given stream
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="streamName">The stream to query</param>
		/// <param name="changeNumber">Shelved changelist number</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Whether the shelf is valid, plus the description for it</returns>
		public Task<(CheckShelfResult, string?)> CheckShelfAsync(string clusterName, string streamName, int changeNumber, CancellationToken cancellationToken = default);

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="stream">Stream to query</param>
		/// <param name="changeNumber">Change numbers to query</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Commit details</returns>
		public Task<ICommit> GetChangeDetailsAsync(IStream stream, int changeNumber, CancellationToken cancellationToken = default);

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="stream">Stream to query</param>
		/// <param name="changeNumbers">Change numbers to query</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Commit details</returns>
		public Task<List<ICommit>> GetChangeDetailsAsync(IStream stream, IReadOnlyList<int> changeNumbers, CancellationToken cancellationToken = default);

		/// <summary>
		/// Duplicates a shelved changelist
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="shelvedChange">The shelved changelist</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The duplicated changelist</returns>
		public Task<int> DuplicateShelvedChangeAsync(string clusterName, int shelvedChange, CancellationToken cancellationToken = default);

		/// <summary>
		/// Submit a shelved changelist
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="shelvedChange">The shelved changelist number, created by <see cref="DuplicateShelvedChangeAsync(String,Int32,CancellationToken)"/></param>
		/// <param name="originalChange">The original changelist number</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Tuple consisting of the submitted changelist number and message</returns>
		public Task<(int? Change, string Message)> SubmitShelvedChangeAsync(string clusterName, int shelvedChange, int originalChange, CancellationToken cancellationToken = default);

		/// <summary>
		/// Deletes a changelist containing shelved files.
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="shelvedChange">The changelist containing shelved files</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Async tasy</returns>
		public Task DeleteShelvedChangeAsync(string clusterName, int shelvedChange, CancellationToken cancellationToken = default);

		/// <summary>
		/// Updates a changelist description
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="change">The change to update</param>
		/// <param name="description">The new description</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Async task</returns>
		public Task UpdateChangelistDescription(string clusterName, int change, string description, CancellationToken cancellationToken = default);
	}

	/// <summary>
	/// Extension methods for IPerforceService implementations
	/// </summary>
	public static class PerforceServiceExtensions
	{
		/// <summary>
		/// Creates a new change for a template
		/// </summary>
		/// <param name="perforce">The Perforce service instance</param>
		/// <param name="stream">Stream containing the template</param>
		/// <param name="template">The template being built</param>
		/// <returns>New changelist number</returns>
		public static Task<int> CreateNewChangeForTemplateAsync(this IPerforceService perforce, IStream stream, ITemplate template)
		{
			string description = (template.SubmitDescription ?? "[Horde] New change for $(TemplateName)").Replace("$(TemplateName)", template.Name, StringComparison.OrdinalIgnoreCase);

			Match match = Regex.Match(template.SubmitNewChange!, @"^(//[^/]+/[^/]+)/(.+)$");
			if (match.Success)
			{
				return perforce.CreateNewChangeAsync(stream.Config.ClusterName, match.Groups[1].Value, match.Groups[2].Value, description);
			}
			else
			{
				return perforce.CreateNewChangeAsync(stream.Config.ClusterName, stream.Name, template.SubmitNewChange!, description);
			}
		}

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="perforce">The perforce implementation</param>
		/// <param name="stream">The stream to query</param>
		/// <param name="changeNumber">Change number to query</param>
		/// <returns>Commit details</returns>
		public static async Task<ICommit> GetChangeDetailsAsync(this IPerforceService perforce, IStream stream, int changeNumber)
		{
			List<ICommit> results = await perforce.GetChangeDetailsAsync(stream, new[] { changeNumber });
			return results[0];
		}

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="perforce">The perforce implementation</param>
		/// <param name="stream">The stream to query</param>
		/// <param name="minChange">The minimum changelist number</param>
		/// <param name="maxChange">The maximum changelist number</param>
		/// <param name="results">Number of results to return</param>
		/// <returns>Commit details</returns>
		public static async Task<List<ICommit>> GetChangeDetailsAsync(this IPerforceService perforce, IStream stream, int? minChange, int? maxChange, int results)
		{
			List<ICommit> changes = await perforce.GetChangesAsync(stream, minChange, maxChange, results);
			return await perforce.GetChangeDetailsAsync(stream, changes.ConvertAll(x => x.Number));
		}

		/// <summary>
		/// Get the latest submitted change to the stream
		/// </summary>
		/// <param name="perforce">The perforce implementation</param>
		/// <param name="stream">The stream to query</param>
		/// <returns>Latest changelist number</returns>
		public static async Task<int> GetLatestChangeAsync(this IPerforceService perforce, IStream stream)
		{
			List<ICommit> changes = await perforce.GetChangesAsync(stream, null, null, 1);
			if (changes.Count == 0)
			{
				throw new Exception($"No changes have been submitted to stream {stream.Name}");
			}
			return changes[0].Number;
		}
	}
}
