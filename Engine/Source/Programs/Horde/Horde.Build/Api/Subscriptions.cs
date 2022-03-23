// Copyright Epic Games, Inc. All Rights Reserved.

using System.ComponentModel.DataAnnotations;
using Horde.Build.Models;

namespace Horde.Build.Api
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
		/// <param name="subscription">The subscription to construct from</param>
		public CreateSubscriptionResponse(ISubscription subscription)
		{
			Id = subscription.Id;
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
		/// <param name="subscription">Subscription to construct from</param>
		public GetSubscriptionResponse(ISubscription subscription)
		{
			Id = subscription.Id;
			Event = subscription.Event.ToRecord();
			UserId = subscription.UserId.ToString();
			NotificationType = subscription.NotificationType;
		}
	}
}
