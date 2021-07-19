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

namespace HordeServer.Services
{
	/// <summary>
	/// Information about a Perforce user
	/// </summary>
	public class PerforceUserInfo
	{
		/// <summary>
		/// Full name of the user
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// User's email address
		/// </summary>
		public string? Email { get; set; }
	}

	/// <summary>
	/// Wrapper around Perforce functionality. Can use a local p4.exe client for development purposes, or a separate HordePerforceBridge instance over REST for deployments.
	/// </summary>
	public interface IPerforceService
	{
		/// <summary>
		/// Create a new changelist by submitting the given file
		/// </summary>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="Path">Path for the file to submit</param>
		/// <returns>New changelist number</returns>
		public Task<int> CreateNewChangeAsync(string StreamName, string Path);

		/// <summary>
		/// Gets the code change corresponding to an actual change submitted to a stream
		/// </summary>
		/// <param name="StreamName"></param>
		/// <param name="Change">The changelist number to query</param>
		/// <returns>Code change for the latest change</returns>
		public Task<int> GetCodeChangeAsync(string StreamName, int Change);

		/// <summary>
		/// Gets information about the given user
		/// </summary>
		/// <param name="UserName">The user name</param>
		/// <returns>User information</returns>
		public Task<PerforceUserInfo?> GetUserInfoAsync(string UserName);

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="MinChange">The minimum changelist number</param>
		/// <param name="MaxChange">The maximum changelist number</param>
		/// <param name="Results">Number of results to return</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Latest changelist number</returns>
		public Task<List<ChangeSummary>> GetChangesAsync(string StreamName, int? MinChange, int? MaxChange, int Results, string? ImpersonateUser);

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="ChangeNumbers">Change numbers to query</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Commit details</returns>
		public Task<List<ChangeDetails>> GetChangeDetailsAsync(string StreamName, IReadOnlyList<int> ChangeNumbers, string? ImpersonateUser);
		
		/// <summary>
		/// Create a ticket as the specified user
		/// </summary>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Commit details</returns>
		public Task<string> CreateTicket(string ImpersonateUser);

		/// <summary>
		/// Gets the latest changes for a set of depot paths
		/// </summary>
		/// <param name="Paths"></param>
		/// <returns></returns>
		public Task<List<FileSummary>> FindFilesAsync(IEnumerable<string> Paths);

		/// <summary>
		/// Gets the contents of a file in the depot
		/// </summary>
		/// <param name="Path">Path to read</param>
		/// <returns>Data for the file</returns>
		public Task<byte[]> PrintAsync(string Path);

		/// <summary>
		/// Duplicates a shelved changelist
		/// </summary>
		/// <param name="ShelvedChange">The shelved changelist</param>
		/// <returns>The duplicated changelist</returns>
		public Task<int> DuplicateShelvedChangeAsync(int ShelvedChange);

		/// <summary>
		/// Submit a shelved changelist
		/// </summary>
		/// <param name="ShelvedChange">The shelved changelist number, created by <see cref="DuplicateShelvedChangeAsync(int)"/></param>
		/// <param name="OriginalChange">The original changelist number</param>
		/// <returns>Tuple consisting of the submitted changelist number and message</returns>
		public Task<(int? Change, string Message)> SubmitShelvedChangeAsync(int ShelvedChange, int OriginalChange);

		/// <summary>
		/// Deletes a changelist containing shelved files.
		/// </summary>
		/// <param name="ShelvedChange">The changelist containing shelved files</param>
		/// <returns>Async tasy</returns>
		public Task DeleteShelvedChangeAsync(int ShelvedChange);

		/// <summary>
		/// Updates a changelist description
		/// </summary>
		/// <param name="Change">The change to update</param>
		/// <param name="Description">The new description</param>
		/// <returns>Async task</returns>
		public Task UpdateChangelistDescription(int Change, string Description);
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
				return Perforce.CreateNewChangeAsync(Match.Groups[1].Value, Match.Groups[2].Value);
			}
			else
			{
				return Perforce.CreateNewChangeAsync(Stream.Name, Template.SubmitNewChange!);
			}
		}

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="Perforce">The perforce implementation</param>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="ChangeNumber">Change number to query</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Commit details</returns>
		public static async Task<ChangeDetails> GetChangeDetailsAsync(this IPerforceService Perforce, string StreamName, int ChangeNumber, string? ImpersonateUser)
		{
			List<ChangeDetails> Results = await Perforce.GetChangeDetailsAsync(StreamName, new[] { ChangeNumber }, ImpersonateUser);
			return Results[0];
		}

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="Perforce">The perforce implementation</param>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="MinChange">The minimum changelist number</param>
		/// <param name="MaxChange">The maximum changelist number</param>
		/// <param name="Results">Number of results to return</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Commit details</returns>
		public static async Task<List<ChangeDetails>> GetChangeDetailsAsync(this IPerforceService Perforce, string StreamName, int? MinChange, int? MaxChange, int Results, string? ImpersonateUser)
		{
			List<ChangeSummary> Changes = await Perforce.GetChangesAsync(StreamName, MinChange, MaxChange, Results, ImpersonateUser);
			return await Perforce.GetChangeDetailsAsync(StreamName, Changes.ConvertAll(x => x.Number), ImpersonateUser);
		}

		/// <summary>
		/// Get the latest submitted change to the stream
		/// </summary>
		/// <param name="Perforce">The perforce implementation</param>
		/// <param name="StreamName">The stream to query</param>
		/// <param name="ImpersonateUser">Name of the user to impersonate</param>
		/// <returns>Latest changelist number</returns>
		public static async Task<int> GetLatestChangeAsync(this IPerforceService Perforce, string StreamName, string? ImpersonateUser)
		{
			List<ChangeSummary> Changes = await Perforce.GetChangesAsync(StreamName, null, null, 1, ImpersonateUser);
			if (Changes.Count == 0)
			{
				throw new Exception($"No changes have been submitted to stream {StreamName}");
			}
			return Changes[0].Number;
		}
	}
}
