// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using HordeServer.Models;
using Microsoft.Extensions.Logging;
using Polly;
using Polly.Extensions.Http;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

using P4 = Perforce.P4;

namespace HordeServer.Services
{
	/// <summary>
	/// Information about a Perforce user
	/// </summary>
	public class PerforceUserInfo
	{
		/// <summary>
		/// Login for the user
		/// </summary>
		public string? Login { get; set; }

		/// <summary>
		/// Full name of the user
		/// </summary>
		public string? FullName { get; set; }

		/// <summary>
		/// User's email address
		/// </summary>
		public string? Email { get; set; }
	}

	/// <summary>
	/// Interface for a Perforce stream view
	/// </summary>
	public interface IStreamView : IDisposable
	{
		/// <summary>
		/// Maps a depot path into the stream
		/// </summary>
		/// <param name="DepotPath">The depot path</param>
		/// <param name="StreamPath">The resulting stream path</param>
		/// <returns></returns>
		bool TryGetStreamPath(string DepotPath, [NotNullWhen(true)] out string? StreamPath);
	}

	/// <summary>
	/// Wrapper around Perforce functionality. Can use a local p4.exe client for development purposes, or a separate HordePerforceBridge instance over REST for deployments.
	/// </summary>
	public interface IPerforceService
	{
		/// <summary>
		/// Finds or adds a user from the given Perforce server, adding the user (and populating their profile with Perforce data) if they do not currently exist
		/// </summary>
		/// <param name="ClusterName"></param>
		/// <param name="UserName"></param>
		/// <returns></returns>
		public ValueTask<IUser> FindOrAddUserAsync(string ClusterName, string UserName);

		/// <summary>
		/// 
		/// </summary>
		/// <param name="ClusterName"></param>
		/// <returns></returns>
		public Task<NativePerforceConnection?> GetServiceUserConnection(string? ClusterName);

		/// <summary>
		/// Gets the definition of a stream
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="StreamName">The stream name</param>
		/// <returns></returns>
		public Task<IStreamView> GetStreamViewAsync(string ClusterName, string StreamName);

		/// <summary>
		/// Gets the state of all files in a stream
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="StreamName">The stream name</param>
		/// <param name="Change">The changelist to query</param>
		/// <returns></returns>
		public Task<List<ChangeFile>> GetStreamSnapshotAsync(string ClusterName, string StreamName, int Change);

		/// <summary>
		/// Create a new changelist by submitting the given file
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="Path">Path for the file to submit</param>
		/// <returns>New changelist number</returns>
		public Task<int> CreateNewChangeAsync(string ClusterName, string StreamName, string Path);

		/// <summary>
		/// Gets the code change corresponding to an actual change submitted to a stream
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="StreamName"></param>
		/// <param name="Change">The changelist number to query</param>
		/// <returns>Code change for the latest change</returns>
		public Task<int> GetCodeChangeAsync(string ClusterName, string StreamName, int Change);

		/// <summary>
		/// Gets information about the given user
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="UserName">The user name</param>
		/// <returns>User information</returns>
		public Task<PerforceUserInfo?> GetUserInfoAsync(string ClusterName, string UserName);

		/// <summary>
		/// Finds changes submitted to a depot Gets the latest change for a particular stream
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="MinChange">The minimum changelist number</param>
		/// <param name="MaxChange">The maximum changelist number</param>
		/// <param name="MaxResults"></param>
		/// <returns>Changelist information</returns>
		public Task<List<ChangeSummary>> GetChangesAsync(string ClusterName, int? MinChange, int? MaxChange, int MaxResults);

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="MinChange">The minimum changelist number</param>
		/// <param name="MaxChange">The maximum changelist number</param>
		/// <param name="Results">Number of results to return</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Latest changelist number</returns>
		public Task<List<ChangeSummary>> GetChangesAsync(string ClusterName, string StreamName, int? MinChange, int? MaxChange, int Results, string? ImpersonateUser);

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="ChangeNumber">Change numbers to query</param>
		/// <returns>Commit details</returns>
		public Task<ChangeDetails> GetChangeDetailsAsync(string ClusterName, string StreamName, int ChangeNumber);

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="ChangeNumbers">Change numbers to query</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Commit details</returns>
		public Task<List<ChangeDetails>> GetChangeDetailsAsync(string ClusterName, string StreamName, IReadOnlyList<int> ChangeNumbers, string? ImpersonateUser);

		/// <summary>
		/// Create a ticket as the specified user
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Commit details</returns>
		public Task<string> CreateTicket(string ClusterName, string ImpersonateUser);

		/// <summary>
		/// Gets the latest changes for a set of depot paths
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="Paths"></param>
		/// <returns></returns>
		public Task<List<FileSummary>> FindFilesAsync(string ClusterName, IEnumerable<string> Paths);

		/// <summary>
		/// Gets the contents of a file in the depot
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="Path">Path to read</param>
		/// <returns>Data for the file</returns>
		public Task<byte[]> PrintAsync(string ClusterName, string Path);

		/// <summary>
		/// Duplicates a shelved changelist
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="ShelvedChange">The shelved changelist</param>
		/// <returns>The duplicated changelist</returns>
		public Task<int> DuplicateShelvedChangeAsync(string ClusterName, int ShelvedChange);

		/// <summary>
		/// Submit a shelved changelist
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="ShelvedChange">The shelved changelist number, created by <see cref="DuplicateShelvedChangeAsync(string,int)"/></param>
		/// <param name="OriginalChange">The original changelist number</param>
		/// <returns>Tuple consisting of the submitted changelist number and message</returns>
		public Task<(int? Change, string Message)> SubmitShelvedChangeAsync(string ClusterName, int ShelvedChange, int OriginalChange);

		/// <summary>
		/// Deletes a changelist containing shelved files.
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="ShelvedChange">The changelist containing shelved files</param>
		/// <returns>Async tasy</returns>
		public Task DeleteShelvedChangeAsync(string ClusterName, int ShelvedChange);

		/// <summary>
		/// Updates a changelist description
		/// </summary>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="Change">The change to update</param>
		/// <param name="Description">The new description</param>
		/// <returns>Async task</returns>
		public Task UpdateChangelistDescription(string ClusterName, int Change, string Description);
	}

	/// <summary>
	/// Extension methods for IPerforceService implementations
	/// </summary>
	public static class PerforceServiceExtensions
	{
		/// <summary>
		/// Creates a new change for a template
		/// </summary>
		/// <param name="Perforce">The Perforce service instance</param>
		/// <param name="Stream">Stream containing the template</param>
		/// <param name="Template">The template being built</param>
		/// <returns>New changelist number</returns>
		public static Task<int> CreateNewChangeForTemplateAsync(this IPerforceService Perforce, IStream Stream, ITemplate Template)
		{
			Match Match = Regex.Match(Template.SubmitNewChange!, @"^(//[^/]+/[^/]+)/(.+)$");
			if (Match.Success)
			{
				return Perforce.CreateNewChangeAsync(Stream.ClusterName, Match.Groups[1].Value, Match.Groups[2].Value);
			}
			else
			{
				return Perforce.CreateNewChangeAsync(Stream.ClusterName, Stream.Name, Template.SubmitNewChange!);
			}
		}

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="Perforce">The perforce implementation</param>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="ChangeNumber">Change number to query</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Commit details</returns>
		public static async Task<ChangeDetails> GetChangeDetailsAsync(this IPerforceService Perforce, string ClusterName, string StreamName, int ChangeNumber, string? ImpersonateUser)
		{
			List<ChangeDetails> Results = await Perforce.GetChangeDetailsAsync(ClusterName, StreamName, new[] { ChangeNumber }, ImpersonateUser);
			return Results[0];
		}

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="Perforce">The perforce implementation</param>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="MinChange">The minimum changelist number</param>
		/// <param name="MaxChange">The maximum changelist number</param>
		/// <param name="Results">Number of results to return</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Commit details</returns>
		public static async Task<List<ChangeDetails>> GetChangeDetailsAsync(this IPerforceService Perforce, string ClusterName, string StreamName, int? MinChange, int? MaxChange, int Results, string? ImpersonateUser)
		{
			List<ChangeSummary> Changes = await Perforce.GetChangesAsync(ClusterName, StreamName, MinChange, MaxChange, Results, ImpersonateUser);
			return await Perforce.GetChangeDetailsAsync(ClusterName, StreamName, Changes.ConvertAll(x => x.Number), ImpersonateUser);
		}

		/// <summary>
		/// Get the latest submitted change to the stream
		/// </summary>
		/// <param name="Perforce">The perforce implementation</param>
		/// <param name="ClusterName">Name of the Perforce cluster</param>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Latest changelist number</returns>
		public static async Task<int> GetLatestChangeAsync(this IPerforceService Perforce, string ClusterName, string StreamName, string? ImpersonateUser)
		{
			List<ChangeSummary> Changes = await Perforce.GetChangesAsync(ClusterName, StreamName, null, null, 1, ImpersonateUser);
			if (Changes.Count == 0)
			{
				throw new Exception($"No changes have been submitted to stream {StreamName}");
			}
			return Changes[0].Number;
		}
	}
}
