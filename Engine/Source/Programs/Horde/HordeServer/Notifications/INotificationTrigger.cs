// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System.Collections.Generic;

namespace HordeServer.Models
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// An individual subscription
	/// </summary>
	public interface INotificationSubscription
	{
		/// <summary>
		/// Name of the user
		/// </summary>
		public UserId UserId { get; }

		/// <summary>
		/// Whether to receive email notifications
		/// </summary>
		public bool Email { get; }

		/// <summary>
		/// Whether to receive Slack notifications
		/// </summary>
		public bool Slack { get; }
	}

	/// <summary>
	/// Trigger for notifications to be sent
	/// </summary>
	public interface INotificationTrigger
	{
		/// <summary>
		/// Unique id for this subscription list
		/// </summary>
		ObjectId Id { get; }

		/// <summary>
		/// Whether this trigger has been fired
		/// </summary>
		bool Fired { get; }

		/// <summary>
		/// List of subscriptions to this event
		/// </summary>
		public IReadOnlyList<INotificationSubscription> Subscriptions { get; }
	}
}
