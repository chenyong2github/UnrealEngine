// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Text.Json;
using EpicGames.Core;
using Horde.Server.Acls;
using Horde.Server.Agents;
using Horde.Server.Agents.Pools;
using Horde.Server.Server;
using Horde.Server.Server.Notices;
using MongoDB.Bson;

namespace Horde.Server.Users
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
		/// Settings for the dashboard
		/// </summary>
		public object? DashboardSettings { get; set; }

		/// <summary>
		/// Settings for whether various dashboard features should be shown for the current user
		/// </summary>
		public GetDashboardFeaturesResponse? DashboardFeatures { get; set; }

		/// <summary>
		/// List of pinned job ids
		/// </summary>
		public List<string>? PinnedJobIds { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public GetUserResponse(IUser user, IAvatar? avatar, IUserClaims? claims, IUserSettings? settings)
		{
			Id = user.Id.ToString();
			Name = user.Name;
			Email = user.Email;

			Image24 = avatar?.Image24;
			Image32 = avatar?.Image32;
			Image48 = avatar?.Image48;
			Image72 = avatar?.Image72;
						
			Claims = claims?.Claims.Select(x => new UserClaim(x)).ToList();

			if (settings != null)
			{
				EnableExperimentalFeatures = settings.EnableExperimentalFeatures;
				
				DashboardSettings = BsonTypeMapper.MapToDotNetValue(settings.DashboardSettings);
				PinnedJobIds = settings.PinnedJobIds.ConvertAll(x => x.ToString());
			}
		}
	}

	/// <summary>
	/// Settings for whether various features should be enabled on the dashboard
	/// </summary>
	public class GetDashboardFeaturesResponse
	{
		/// <summary>
		/// Enable CI functionality
		/// </summary>
		public bool ShowCI { get; set; }

		/// <summary>
		/// Show the Perforce server option on the server menu
		/// </summary>
		public bool ShowPerforceServers { get; set; }

		/// <summary>
		/// Show the device manager on the server menu
		/// </summary>
		public bool ShowDeviceManager { get; set; }

		/// <summary>
		/// Show automation on the server menu
		/// </summary>
		public bool ShowAutomation { get; set; }

		/// <summary>
		/// Whether the notice editor should be listed in the server menu
		/// </summary>
		public bool ShowNoticeEditor { get; set; }

		/// <summary>
		/// Whether controls for modifying pools should be shown
		/// </summary>
		public bool ShowPoolEditor { get; set; }

		/// <summary>
		/// Whether the remote desktop button should be shown on the agent modal
		/// </summary>
		public bool ShowRemoteDesktop { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public GetDashboardFeaturesResponse(GlobalConfig globalConfig, ClaimsPrincipal principal)
		{
			ShowCI = true;
			ShowPerforceServers = true;
			ShowDeviceManager = true;
			ShowAutomation = true;
			ShowNoticeEditor = globalConfig.Authorize(NoticeAclAction.CreateNotice, principal) || globalConfig.Authorize(NoticeAclAction.UpdateNotice, principal);
			ShowPoolEditor = globalConfig.Authorize(PoolAclAction.CreatePool, principal) || globalConfig.Authorize(PoolAclAction.UpdatePool, principal);
			ShowRemoteDesktop = globalConfig.Authorize(AgentAclAction.UpdateAgent, principal);
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
		/// <param name="user"></param>
		public GetThinUserInfoResponse(IUser? user)
		{
			if (user == null)
			{
				Id = String.Empty;
				Name = "(Unknown)";
				Email = null;
				Login = null;
			}
			else
			{
				Id = user.Id.ToString();
				Name = user.Name;
				Email = user.Email;
				Login = user.Login;
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
