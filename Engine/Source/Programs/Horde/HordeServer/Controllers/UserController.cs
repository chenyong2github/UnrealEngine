using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/user endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class UserController : ControllerBase
	{
		/// <summary>
		/// The user collection instance
		/// </summary>
		IUserCollection UserCollection { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="UserCollection"></param>
		public UserController(IUserCollection UserCollection)
		{
			this.UserCollection = UserCollection;
		}

		/// <summary>
		/// Gets information about the logged in user
		/// </summary>
		/// <returns>Http result code</returns>
		[HttpGet]
		[Route("/api/v1/user")]
		[ProducesResponseType(typeof(List<GetUserResponse>), 200)]
		public async Task<ActionResult<object>> GetUserAsync([FromQuery] PropertyFilter? Filter = null)
		{
			IUser? InternalUser = await UserCollection.GetUserAsync(User);
			if (InternalUser == null)
			{
				return NotFound();
			}

			IUserClaims? Claims = await UserCollection.GetClaimsAsync(InternalUser.Id);
			IUserSettings? Settings = await UserCollection.GetSettingsAsync(InternalUser.Id);
			return PropertyFilter.Apply(new GetUserResponse(InternalUser, Claims, Settings), Filter);
		}

		/// <summary>
		/// Updates the logged in user
		/// </summary>
		/// <returns>Http result code</returns>
		[HttpPut]
		[Route("/api/v1/user")]
		public async Task<ActionResult> UpdateUserAsync(UpdateUserRequest Request)
		{
			ObjectId? UserId = User.GetUserId();
			if(UserId == null)
			{
				return BadRequest("Current user does not have a registered profile");
			}
			await UserCollection.UpdateSettingsAsync(UserId.Value, Request.EnableExperimentalFeatures, Request.EnableIssueNotifications, Request.DashboardSettings?.ToBsonValue(), Request.AddPinnedJobIds?.Select(x => new ObjectId(x)), Request.RemovePinnedJobIds?.Select(x => new ObjectId(x)));
			return Ok();
		}
	}
}
