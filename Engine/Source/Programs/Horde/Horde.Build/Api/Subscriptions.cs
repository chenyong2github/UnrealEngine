// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Request object for creating a new subscription
	/// </summary>
	public class CreateSubscriptionRequest
	{
		/// <summary>
		/// The event to create a subscription for
		/// </summary>
		[Required]
		public EventRecord Event { get; set; } = null!;

		/// <summary>
		/// The user to subscribe. Defaults to the current user if unspecified.
		/// </summary>
		[Required]
		public string UserId { get; set; } = null!;

		/// <summary>
		/// Type of notification to send
		/// </summary>
		public NotificationType NotificationType { get; set; }
	}

	/// <summary>
	/// Response from creating a subscription
	/// </summary>
	public class CreateSubscriptionResponse
	{
		/// <summary>
		/// The subscription id
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Subscription">The subscription to construct from</param>
		public CreateSubscriptionResponse(ISubscription Subscription)
		{
			this.Id = Subscription.Id;
		}
	}

	/// <summary>
	/// Response describing a subscription
	/// </summary>
	public class GetSubscriptionResponse
	{
		/// <summary>
		/// The subscription id
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Information about the specific event
		/// </summary>
		public EventRecord Event { get; set; }

		/// <summary>
		/// Unique id of the user subscribed to the event
		/// </summary>
		public string UserId { get; set; }

		/// <summary>
		/// The type of notification to receive
		/// </summary>
		public NotificationType NotificationType { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Subscription">Subscription to construct from</param>
		public GetSubscriptionResponse(ISubscription Subscription)
		{
			this.Id = Subscription.Id;
			this.Event = Subscription.Event.ToRecord();
			this.UserId = Subscription.UserId.ToString();
			this.NotificationType = Subscription.NotificationType;
		}
	}
}
