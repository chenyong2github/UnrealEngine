// Copyright Epic Games, Inc. All Rights Reserved.

using System.ComponentModel.DataAnnotations;
using HordeServer.Models;

namespace HordeServer.Api
{
	/// <summary>
	/// A request from a user to be notified when something happens.
	/// </summary>
	public class UpdateNotificationsRequest
	{
		/// <summary>
		/// Request notifications by email
		/// </summary>
		public bool? Email { get; set; }

		/// <summary>
		/// Request notifications on Slack
		/// </summary>
		public bool? Slack { get; set; }
	}

	/// <summary>
	/// A request from a user to be notified when something happens.
	/// </summary>
	public class GetNotificationResponse
	{
		/// <summary>
		/// Request notifications by email
		/// </summary>
		public bool Email { get; set; }

		/// <summary>
		/// Request notifications on Slack
		/// </summary>
		public bool Slack { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Subscription">The subscription interface</param>
		internal GetNotificationResponse(INotificationSubscription? Subscription)
		{
			if (Subscription == null)
			{
				Email = false;
				Slack = false;
			}
			else
			{
				Email = Subscription.Email;
				Slack = Subscription.Slack;
			}
		}
	}
}
