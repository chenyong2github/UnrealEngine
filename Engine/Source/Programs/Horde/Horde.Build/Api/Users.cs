// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Response describing the current user
	/// </summary>
	public class GetUserResponse
	{
		/// <summary>
		/// Id of the user
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Name of the user
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Avatar image URL (24px)
		/// </summary>
		public string? Image24 { get; set; }

		/// <summary>
		/// Avatar image URL (32px)
		/// </summary>
		public string? Image32 { get; set; }

		/// <summary>
		/// Avatar image URL (48px)
		/// </summary>
		public string? Image48 { get; set; }

		/// <summary>
		/// Avatar image URL (72px)
		/// </summary>
		public string? Image72 { get; set; }

		/// <summary>
		/// Email of the user
		/// </summary>
		public string? Email { get; set; }

		/// <summary>
		/// Claims for the user
		/// </summary>
		public List<UserClaim>? Claims { get; set; }

		/// <summary>
		/// Whether to enable experimental features for this user
		/// </summary>
		public bool? EnableExperimentalFeatures { get; set; }

		/// <summary>
		/// Whether to enable slack notifications for this user
		/// </summary>
		public bool? EnableIssueNotifications { get; set; }

		/// <summary>
		/// Settings for the dashboard
		/// </summary>
		public object? DashboardSettings { get; set; }

		/// <summary>
		/// List of pinned job ids
		/// </summary>
		public List<string>? PinnedJobIds { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public GetUserResponse(IUser User, IAvatar? Avatar, IUserClaims? Claims, IUserSettings? Settings)
		{
			this.Id = User.Id.ToString();
			this.Name = User.Name;
			this.Email = User.Email;

			this.Image24 = Avatar?.Image24;
			this.Image32 = Avatar?.Image32;
			this.Image48 = Avatar?.Image48;
			this.Image72 = Avatar?.Image72;
						
			this.Claims = Claims == null ? null : Claims.Claims.Select(x => new UserClaim(x)).ToList();

			if (Settings != null)
			{
				this.EnableExperimentalFeatures = Settings.EnableExperimentalFeatures;
				this.EnableIssueNotifications = Settings.EnableIssueNotifications;
				this.DashboardSettings = BsonTypeMapper.MapToDotNetValue(Settings.DashboardSettings);
				this.PinnedJobIds = Settings.PinnedJobIds.ConvertAll(x => x.ToString());
			}
		}
	}

	/// <summary>
	/// Basic information about a user. May be embedded in other responses.
	/// </summary>
	public class GetThinUserInfoResponse
	{
		/// <summary>
		/// Id of the user
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Name of the user
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// The user's email address
		/// </summary>
		public string? Email { get; set; }

		/// <summary>
		/// The user login [DEPRECATED]
		/// </summary>
		internal string? Login { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="User"></param>
		public GetThinUserInfoResponse(IUser? User)
		{
			if (User == null)
			{
				this.Id = String.Empty;
				this.Name = "(Unknown)";
				this.Email = null;
				this.Login = null;
			}
			else
			{
				this.Id = User.Id.ToString();
				this.Name = User.Name;
				this.Email = User.Email;
				this.Login = User.Login;
			}
		}
	}

	/// <summary>
	/// Request to update settings for a user
	/// </summary>
	public class UpdateUserRequest
	{
		/// <summary>
		/// Whether to enable experimental features for this user
		/// </summary>
		public bool? EnableExperimentalFeatures { get; set; }

		/// <summary>
		/// Whether to enable Slack notifications for this user
		/// </summary>
		public bool? EnableIssueNotifications { get; set; }

		/// <summary>
		/// New dashboard settings
		/// </summary>
		public JsonElement? DashboardSettings { get; set; }

		/// <summary>
		/// Job ids to add to the pinned list
		/// </summary>
		public List<string>? AddPinnedJobIds { get; set; }

		/// <summary>
		/// Jobs ids to remove from the pinned list
		/// </summary>
		public List<string>? RemovePinnedJobIds { get; set; }
	}

}
