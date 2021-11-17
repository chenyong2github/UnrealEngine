// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using JobId = ObjectId<IJob>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Manages user documents
	/// </summary>
	public interface IUserCollection
	{
		/// <summary>
		/// Gets a user by unique id
		/// </summary>
		/// <param name="Id">Id of the user</param>
		/// <returns>The user information</returns>
		Task<IUser?> GetUserAsync(UserId Id);

		/// <summary>
		/// Gets a cached user by unique id
		/// </summary>
		/// <param name="Id">Id of the user</param>
		/// <returns>The user information</returns>
		ValueTask<IUser?> GetCachedUserAsync(UserId? Id);

		/// <summary>
		/// Gets a user by unique id
		/// </summary>
		/// <param name="Ids">Ids of the users</param>
		/// <param name="NameRegex">Name regex to match for the users</param>
		/// <param name="Index">Maximum number of results</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>The user information</returns>
		Task<List<IUser>> FindUsersAsync(IEnumerable<UserId>? Ids = null, string? NameRegex = null, int? Index = null, int? Count = null);

		/// <summary>
		/// Gets a user by login
		/// </summary>
		/// <param name="Login">Login for the user</param>
		/// <returns>The user information</returns>
		Task<IUser?> FindUserByLoginAsync(string Login);

		/// <summary>
		/// Find or add a user with the given claims. Claims will be updated if the user exists.
		/// </summary>
		/// <param name="Login">Login id of the user</param>
		/// <param name="Name">Full name of the user</param>
		/// <param name="Email">Email address of the user</param>
		/// <returns>The user document</returns>
		Task<IUser> FindOrAddUserByLoginAsync(string Login, string? Name = null, string? Email = null);

		/// <summary>
		/// Gets the claims for a user
		/// </summary>
		/// <param name="UserId"></param>
		/// <returns></returns>
		Task<IUserClaims> GetClaimsAsync(UserId UserId);

		/// <summary>
		/// Update the claims for a user
		/// </summary>
		/// <param name="UserId"></param>
		/// <param name="Claims"></param>
		/// <returns></returns>
		Task UpdateClaimsAsync(UserId UserId, IEnumerable<IUserClaim> Claims);

		/// <summary>
		/// Get settings for a user
		/// </summary>
		/// <param name="UserId"></param>
		/// <returns></returns>
		Task<IUserSettings> GetSettingsAsync(UserId UserId);

		/// <summary>
		/// Update a user
		/// </summary>
		/// <param name="UserId">The user to update</param>
		/// <param name="EnableExperimentalFeatures"></param>
		/// <param name="EnableIssueNotifications"></param>
		/// <param name="DashboardSettings">Opaque settings object for the dashboard</param>
		/// <param name="AddPinnedJobIds"></param>
		/// <param name="RemovePinnedJobIds"></param>
		/// <returns>Updated user object</returns>
		Task UpdateSettingsAsync(UserId UserId, bool? EnableExperimentalFeatures = null, bool? EnableIssueNotifications = null, BsonValue? DashboardSettings = null, IEnumerable<JobId>? AddPinnedJobIds = null, IEnumerable<JobId>? RemovePinnedJobIds = null);
	}

	/// <summary>
	/// Extension methods for the user collect
	/// </summary>
	static class UserCollectionExtensions
	{
		/// <summary>
		/// Gets a particular user info from the collection
		/// </summary>
		/// <param name="UserCollection"></param>
		/// <param name="Principal"></param>
		/// <returns></returns>
		public static Task<IUser?> GetUserAsync(this IUserCollection UserCollection, ClaimsPrincipal Principal)
		{
			UserId? UserId = Principal.GetUserId();
			if (UserId != null)
			{
				return UserCollection.GetUserAsync(UserId.Value);
			}
			else
			{
				return Task.FromResult<IUser?>(null);
			}
		}
	}
}
