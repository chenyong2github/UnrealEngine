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
	using UserId = ObjectId<IUser>;

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
		/// Gets information about a user by id, specify "current" for id to get the currently logged in user
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

		/// <summary>
		/// Gets a list of users
		/// </summary>
		/// <returns>List of user responses</returns>
		[HttpGet]
		[Route("/api/v1/users")]
		[ProducesResponseType(typeof(List<GetUserResponse>), 200)]
		public async Task<ActionResult<List<GetUserResponse>>> FindUsersAsync(
			[FromQuery] string[]? Ids = null,
			[FromQuery] string? NameRegex = null,
			[FromQuery] int Index = 0,
			[FromQuery] int Count = 100,
			[FromQuery] bool IncludeClaims = false,
			[FromQuery] bool IncludeAvatar = false)
		{

			UserId[]? UserIds = null;
			if (Ids != null && Ids.Length > 0)
			{
				UserIds = Ids.Select(x => new UserId(x)).ToArray();
			}

			List<IUser> Users = await UserCollection.FindUsersAsync(UserIds, NameRegex, Index, Count);

			List<GetUserResponse> Response = new List<GetUserResponse>();
			foreach (IUser User in Users)
			{
				IAvatar? Avatar = (AvatarService == null || !IncludeAvatar) ? (IAvatar?)null : await AvatarService.GetAvatarAsync(User);
				IUserClaims? Claims = (!IncludeClaims) ? null : await UserCollection.GetClaimsAsync(User.Id);
				Response.Add(new GetUserResponse(User, Avatar, Claims, null));
			}

			return Response;
		}


		async Task<IUser?> GetUserInternalAsync(string Id)
		{
			UserId? UserId = ParseUserId(Id);
			if(UserId == null)
			{
				return null;
			}
			return await UserCollection.GetUserAsync(UserId.Value);
		}

		UserId? ParseUserId(string Id)
		{
			if (Id.Equals("current", StringComparison.OrdinalIgnoreCase))
			{
				return User.GetUserId();
			}
			else if(UserId.TryParse(Id, out UserId Result))
			{
				return Result;
			}
			return null;
		}
	}
}
