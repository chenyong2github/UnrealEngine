// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Notifications.Impl;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/users endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class UsersController : ControllerBase
	{
		/// <summary>
		/// The user collection instance
		/// </summary>
		IUserCollection UserCollection { get; set; }

		/// <summary>
		/// The avatar service
		/// </summary>
		IAvatarService? AvatarService { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="UserCollection"></param>
		/// <param name="AvatarService"></param>
		public UsersController(IUserCollection UserCollection, IAvatarService? AvatarService)
		{
			this.UserCollection = UserCollection;
			this.AvatarService = AvatarService;
		}

		/// <summary>
		/// Gets information about the logged in user
		/// </summary>
		/// <returns>Http result code</returns>
		[HttpGet]
		[Route("/api/v1/users/{Id}")]
		[ProducesResponseType(typeof(List<GetUserResponse>), 200)]
		public async Task<ActionResult<object>> GetUserAsync(string Id, [FromQuery] PropertyFilter? Filter = null)
		{
			IUser? User = await GetUserInternalAsync(Id);
			if (User == null)
			{
				return NotFound();
			}

			IAvatar? Avatar = (AvatarService == null) ? (IAvatar?)null : await AvatarService.GetAvatarAsync(User);
			IUserClaims? Claims = await UserCollection.GetClaimsAsync(User.Id);
			IUserSettings? Settings = await UserCollection.GetSettingsAsync(User.Id);
			return PropertyFilter.Apply(new GetUserResponse(User, Avatar, Claims, Settings), Filter);
		}

		async Task<IUser?> GetUserInternalAsync(string Id)
		{
			ObjectId? UserId = ParseUserId(Id);
			if(UserId == null)
			{
				return null;
			}
			return await UserCollection.GetUserAsync(UserId.Value);
		}

		ObjectId? ParseUserId(string Id)
		{
			if (Id.Equals("current", StringComparison.OrdinalIgnoreCase))
			{
				return User.GetUserId();
			}
			else if(ObjectId.TryParse(Id, out ObjectId Result))
			{
				return Result;
			}
			return null;
		}
	}
}
