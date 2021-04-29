using EpicGames.Core;
using HordeServer.Models;
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
		/// Claims for the user
		/// </summary>
		public List<UserClaim> Claims { get; set; }

		/// <summary>
		/// Whether to enable experimental features for this user
		/// </summary>
		public bool EnableExperimentalFeatures { get; set; }

		/// <summary>
		/// Whether to enable slack notifications for this user
		/// </summary>
		public bool EnableIssueNotifications { get; set; }

		/// <summary>
		/// Settings for the dashboard
		/// </summary>
		public object DashboardSettings { get; set; }

		/// <summary>
		/// List of pinned job ids
		/// </summary>
		public List<string> PinnedJobIds { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="User"></param>
		/// <param name="Claims"></param>
		/// <param name="Settings"></param>
		public GetUserResponse(IUser User, IUserClaims Claims, IUserSettings Settings)
		{
			this.Id = User.Id.ToString();
			this.Claims = Claims.Claims.Select(x => new UserClaim(x)).ToList();
			this.EnableExperimentalFeatures = Settings.EnableExperimentalFeatures;
			this.EnableIssueNotifications = Settings.EnableIssueNotifications;
			this.DashboardSettings = BsonTypeMapper.MapToDotNetValue(Settings.DashboardSettings);
			this.PinnedJobIds = Settings.PinnedJobIds.ConvertAll(x => x.ToString());
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
