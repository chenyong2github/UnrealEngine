// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// Document which collates information about a user, and their personal settings
	/// </summary>
	public interface IUser
	{
		/// <summary>
		/// The user id
		/// </summary>
		public ObjectId Id { get; }

		/// <summary>
		/// Full name of the user
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// The user's login id
		/// </summary>
		public string Login { get; }

		/// <summary>
		/// The user's email
		/// </summary>
		public string? Email { get; }
	}

	/// <summary>
	/// Claims for a particular user
	/// </summary>
	public interface IUserClaims
	{
		/// <summary>
		/// The user id
		/// </summary>
		public ObjectId UserId { get; }

		/// <summary>
		/// Claims for this user (on last login)
		/// </summary>
		public IReadOnlyList<IUserClaim> Claims { get; }
	}

	/// <summary>
	/// User settings document
	/// </summary>
	public interface IUserSettings
	{
		/// <summary>
		/// The user id
		/// </summary>
		public ObjectId UserId { get; }

		/// <summary>
		/// Whether to enable experimental features
		/// </summary>
		public bool EnableExperimentalFeatures { get; }

		/// <summary>
		/// Whether to enable Slack notifications
		/// </summary>
		public bool EnableIssueNotifications { get; }

		/// <summary>
		/// Opaque settings dictionary for the dashboard
		/// </summary>
		public BsonValue DashboardSettings { get; }

		/// <summary>
		/// List of pinned jobs
		/// </summary>
		public IReadOnlyList<ObjectId> PinnedJobIds { get; }
	}
}
