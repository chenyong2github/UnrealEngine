// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Collections;
using HordeServer.Models;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Claim types that are specific to horde
	/// </summary>
	public static class HordeClaimTypes
	{
		/// <summary>
		/// Base URI for all Horde claims.
		/// </summary>
		const string Prefix = "http://epicgames.com/ue/horde/";

		/// <summary>
		/// Claim for a particular role.
		/// </summary>
		public const string Role = Prefix + "role";

		/// <summary>
		/// Claim for a well known internal role (eg. admin)
		/// </summary>
		public const string InternalRole = Prefix + "internal-role";

		/// <summary>
		/// Claim for a particular session
		/// </summary>
		public const string AgentSessionId = Prefix + "session";

		/// <summary>
		/// User name for horde
		/// </summary>
		public const string User = Prefix + "user";

		/// <summary>
		/// Unique id of the horde user (see <see cref="IUserCollection"/>)
		/// </summary>
		public const string UserId = Prefix + "user-id-v2";

		/// <summary>
		/// Claim for downloading artifacts from a particular job
		/// </summary>
		public const string JobArtifacts = Prefix + "job-artifacts";

		/// <summary>
		/// Claim for the Perforce username
		/// </summary>
		public const string PerforceUser = Prefix + "perforce-user";
	}

	/// <summary>
	/// Extension methods for getting Horde claims from a principal
	/// </summary>
	public static class HordeClaimExtensions
	{
		/// <summary>
		/// Gets the Horde user id from a principal
		/// </summary>
		/// <param name="Principal"></param>
		/// <returns></returns>
		public static UserId? GetUserId(this ClaimsPrincipal Principal)
		{
			string? IdValue = Principal.FindFirstValue(HordeClaimTypes.UserId);
			if(IdValue == null)
			{
				return null;
			}
			else
			{
				return new UserId(IdValue);
			}
		}

		/// <summary>
		/// Gets the Horde user name from a principal
		/// </summary>
		/// <param name="Principal"></param>
		/// <returns></returns>
		public static string? GetUserName(this ClaimsPrincipal Principal)
		{
			return Principal.FindFirstValue(HordeClaimTypes.User) ?? Principal.FindFirstValue(ClaimTypes.Name);
		}

		/// <summary>
		/// Get the email for the given user
		/// </summary>
		/// <param name="User">The user to query the email for</param>
		/// <returns>The user's email address or null if not found</returns>
		public static string? GetEmail(this ClaimsPrincipal User)
		{
			return User.Claims.FirstOrDefault(x => x.Type == ClaimTypes.Email)?.Value;
		}

		/// <summary>
		/// Gets the perforce username for the given principal
		/// </summary>
		/// <param name="User">The principal to get the Perforce user for</param>
		/// <returns>Perforce user name</returns>
		public static string? GetPerforceUser(this ClaimsPrincipal User)
		{
			Claim? Claim = User.FindFirst(HordeClaimTypes.PerforceUser);
			if (Claim == null)
			{
				return null;
			}
			return Claim.Value;
		}
	}
}
