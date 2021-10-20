// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Controller for the /api/v1/agents endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class SubscriptionsController : ControllerBase
	{
		/// <summary>
		/// The ACL service singleton
		/// </summary>
		AclService AclService;

		/// <summary>
		/// Collection of subscription documents
		/// </summary>
		ISubscriptionCollection SubscriptionCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The acl service singleton</param>
		/// <param name="SubscriptionCollection">The collection of subscription documents</param>
		public SubscriptionsController(AclService AclService, ISubscriptionCollection SubscriptionCollection)
		{
			this.AclService = AclService;
			this.SubscriptionCollection = SubscriptionCollection;
		}

		/// <summary>
		/// Find subscriptions matching a criteria
		/// </summary>
		/// <param name="UserId">Name of the user</param>
		/// <param name="Filter">Filter for properties to return</param>
		/// <returns>List of subscriptions</returns>
		[HttpGet]
		[Route("/api/v1/subscriptions")]
		[ProducesResponseType(typeof(List<GetSubscriptionResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetSubscriptionsAsync([FromQuery] string UserId, [FromQuery] PropertyFilter? Filter = null)
		{
			UserId UserIdValue;
			if (!TryParseUserId(UserId, out UserIdValue))
			{
				return BadRequest("Invalid user id");
			}
			if (!await AclService.AuthorizeAsUserAsync(User, UserIdValue))
			{
				return Forbid();
			}

			List<ISubscription> Results = await SubscriptionCollection.FindSubscriptionsAsync(UserIdValue);
			return Results.ConvertAll(x => PropertyFilter.Apply(new GetSubscriptionResponse(x), Filter));
		}

		/// <summary>
		/// Find subscriptions matching a criteria
		/// </summary>
		/// <param name="SubscriptionId">The subscription id</param>
		/// <param name="Filter">Filter for properties to return</param>
		/// <returns>List of subscriptions</returns>
		[HttpGet]
		[Route("/api/v1/subscriptions/{SubscriptionId}")]
		[ProducesResponseType(typeof(GetSubscriptionResponse), 200)]
		public async Task<ActionResult<object>> GetSubscriptionAsync(string SubscriptionId, [FromQuery] PropertyFilter? Filter = null)
		{
			ISubscription? Subscription = await SubscriptionCollection.GetAsync(SubscriptionId);
			if (Subscription == null)
			{
				return NotFound();
			}
			if (!await AclService.AuthorizeAsUserAsync(User, Subscription.UserId))
			{
				return Forbid();
			}

			return PropertyFilter.Apply(new GetSubscriptionResponse(Subscription), Filter);
		}


		/// <summary>
		/// Remove a subscription
		/// </summary>
		/// <param name="SubscriptionId">The subscription id</param>
		/// <returns>Async task</returns>
		[HttpDelete]
		[Route("/api/v1/subscriptions/{SubscriptionId}")]
		[ProducesResponseType(typeof(List<GetSubscriptionResponse>), 200)]
		public async Task<ActionResult> DeleteSubscriptionAsync(string SubscriptionId)
		{
			ISubscription? Subscription = await SubscriptionCollection.GetAsync(SubscriptionId);
			if (Subscription == null)
			{
				return NotFound();
			}
			if (!await AclService.AuthorizeAsUserAsync(User, Subscription.UserId))
			{
				return Forbid();
			}

			await SubscriptionCollection.RemoveAsync(new[] { Subscription });
			return Ok();
		}

		/// <summary>
		/// Find subscriptions matching a criteria
		/// </summary>
		/// <param name="Subscriptions">The new subscriptions to create</param>
		/// <returns>List of subscriptions</returns>
		[HttpPost]
		[Route("/api/v1/subscriptions")]
		public async Task<ActionResult<List<CreateSubscriptionResponse>>> CreateSubscriptionsAsync(List<CreateSubscriptionRequest> Subscriptions)
		{
			HashSet<UserId> AuthorizedUsers = new HashSet<UserId>();

			UserId? CurrentUserId = User.GetUserId();
			if(CurrentUserId != null)
			{
				AuthorizedUsers.Add(CurrentUserId.Value);
			}

			GlobalPermissionsCache Cache = new GlobalPermissionsCache();

			List<NewSubscription> NewSubscriptions = new List<NewSubscription>();
			foreach (CreateSubscriptionRequest Subscription in Subscriptions)
			{
				UserId NewUserId;
				if (!TryParseUserId(Subscription.UserId, out NewUserId))
				{
					return BadRequest($"Invalid user id: '{Subscription.UserId}'.");
				}
				if (AuthorizedUsers.Add(NewUserId) && !await AclService.AuthorizeAsync(AclAction.Impersonate, User, Cache))
				{
					return Forbid();
				}
				NewSubscriptions.Add(new NewSubscription(Subscription.Event, NewUserId, Subscription.NotificationType));
			}

			List<ISubscription> Results = await SubscriptionCollection.AddAsync(NewSubscriptions);
			return Results.ConvertAll(x => new CreateSubscriptionResponse(x));
		}

		/// <summary>
		/// Parse a user id from a string. Allows passing the user's name as well as their objectid value.
		/// </summary>
		/// <param name="UserName"></param>
		/// <param name="ObjectId"></param>
		/// <returns></returns>
		bool TryParseUserId(string UserName, out UserId ObjectId)
		{
			UserId NewObjectId;
			if (UserId.TryParse(UserName, out NewObjectId))
			{
				ObjectId = NewObjectId;
				return true;
			}

			string? CurrentUserName = User.GetUserName();
			if (CurrentUserName != null && String.Equals(UserName, CurrentUserName, StringComparison.OrdinalIgnoreCase))
			{
				UserId? CurrentUserId = User.GetUserId();
				if (CurrentUserId != null)
				{
					ObjectId = CurrentUserId.Value;
					return true;
				}
			}

			ObjectId = default;
			return false;
		}
	}
}