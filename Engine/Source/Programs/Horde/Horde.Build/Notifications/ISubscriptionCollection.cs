// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using Polly;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection.Metadata.Ecma335;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Information for creating a new subscription
	/// </summary>
	public class NewSubscription
	{
		/// <summary>
		/// Name of the event
		/// </summary>
		public EventRecord Event { get; set; }

		/// <summary>
		/// The user name
		/// </summary>
		public UserId UserId { get; set; }

		/// <summary>
		/// Type of notification to send
		/// </summary>
		public NotificationType NotificationType { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Event">Name of the event</param>
		/// <param name="UserId">User name</param>
		/// <param name="NotificationType">Notification type</param>
		public NewSubscription(EventRecord Event, UserId UserId, NotificationType NotificationType)
		{
			this.Event = Event;
			this.UserId = UserId;
			this.NotificationType = NotificationType;
		}
	}

	/// <summary>
	/// Interface for a collection of subscriptions
	/// </summary>
	public interface ISubscriptionCollection
	{
		/// <summary>
		/// Add new subscription documents
		/// </summary>
		/// <param name="Subscriptions">The new subscriptions to add</param>
		/// <returns>The subscriptions that were added</returns>
		public Task<List<ISubscription>> AddAsync(IEnumerable<NewSubscription> Subscriptions);

		/// <summary>
		/// Remove a set of existing subscriptions
		/// </summary>
		/// <param name="Subscriptions">Subscriptions to remove</param>
		/// <returns>Async task</returns>
		public Task RemoveAsync(IEnumerable<ISubscription> Subscriptions);

		/// <summary>
		/// Gets a subscription by id
		/// </summary>
		/// <param name="SubscriptionId">Subscription to remove</param>
		/// <returns>Async task</returns>
		public Task<ISubscription?> GetAsync(string SubscriptionId);

		/// <summary>
		/// Find all subscribers of a certain event
		/// </summary>
		/// <param name="Event">Name of the event</param>
		/// <returns>Name of the event to find subscribers for</returns>
		public Task<List<ISubscription>> FindSubscribersAsync(EventRecord Event);

		/// <summary>
		/// Find subscriptions for a particular user
		/// </summary>
		/// <param name="UserId">The user to search for</param>
		/// <returns>List of subscriptions</returns>
		public Task<List<ISubscription>> FindSubscriptionsAsync(UserId UserId);
	}
}
