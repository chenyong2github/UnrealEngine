// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Cached issue information
	/// </summary>
	public interface IIssueDetails
	{
		/// <summary>
		/// The issue instance
		/// </summary>
		IIssue Issue { get; }

		/// <summary>
		/// The user that currently owns this issue
		/// </summary>
		IUser? Owner { get; }

		/// <summary>
		/// The user that nominated the current owner
		/// </summary>
		IUser? NominatedBy { get; }

		/// <summary>
		/// The user that resolved the issue
		/// </summary>
		IUser? ResolvedBy { get; }

		/// <summary>
		/// List of spans for the issue
		/// </summary>
		IReadOnlyList<IIssueSpan> Spans { get; }

		/// <summary>
		/// List of steps for the issue
		/// </summary>
		IReadOnlyList<IIssueStep> Steps { get; }

		/// <summary>
		/// List of suspects for the issue
		/// </summary>
		IReadOnlyList<IIssueSuspect> Suspects { get; }

		/// <summary>
		/// List of users that are suspects for this issue
		/// </summary>
		IReadOnlyList<IUser> SuspectUsers { get; }

		/// <summary>
		/// Determines whether the given user should be notified about the given issue
		/// </summary>
		/// <returns>True if the user should be notified for this change</returns>
		bool ShowNotifications();

		/// <summary>
		/// Determines if the issue is relevant to the given user
		/// </summary>
		/// <param name="UserId">The user to query</param>
		/// <returns>True if the issue is relevant to the given user</returns>
		bool IncludeForUser(UserId UserId);
	}

	/// <summary>
	/// Extension methods for IIssueService implementations
	/// </summary>
	public static class IssueDetailsExtensions
	{
		/// <summary>
		/// Gets an issue details object for a specific issue id
		/// </summary>
		/// <param name="IssueService">The issue service</param>
		/// <param name="IssueId">Issue id to query </param>
		/// <returns></returns>
		public static async Task<IIssueDetails?> GetIssueDetailsAsync(this IIssueService IssueService, int IssueId)
		{
			IIssue? Issue = await IssueService.GetIssueAsync(IssueId);
			if(Issue == null)
			{
				return null;
			}
			return await IssueService.GetIssueDetailsAsync(Issue);
		}
	}
}
