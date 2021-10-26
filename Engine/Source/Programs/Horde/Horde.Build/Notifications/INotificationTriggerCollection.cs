// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Collection of notification triggers
	/// </summary>
	public interface INotificationTriggerCollection
	{
		/// <summary>
		/// Finds or adds a trigger
		/// </summary>
		/// <param name="TriggerId">The trigger id</param>
		/// <returns>New trigger document</returns>
		Task<INotificationTrigger> FindOrAddAsync(ObjectId TriggerId);

		/// <summary>
		/// Finds an existing trigger with the given id, or adds one if it does not exist
		/// </summary>
		/// <param name="TriggerId"></param>
		/// <returns></returns>
		Task<INotificationTrigger?> GetAsync(ObjectId TriggerId);

		/// <summary>
		/// Deletes a trigger
		/// </summary>
		/// <param name="TriggerId">The unique trigger id</param>
		/// <returns>Async task</returns>
		Task DeleteAsync(ObjectId TriggerId);

		/// <summary>
		/// Deletes a trigger
		/// </summary>
		/// <param name="TriggerIds">The unique trigger id</param>
		/// <returns>Async task</returns>
		Task DeleteAsync(List<ObjectId> TriggerIds);

		/// <summary>
		/// Fires the trigger, and marks notifications has having been sent
		/// </summary>
		/// <param name="Trigger">The trigger to fire</param>
		/// <returns>The trigger document</returns>
		Task<INotificationTrigger?> FireAsync(INotificationTrigger Trigger);

		/// <summary>
		/// Adds a subscriber to a particular trigger
		/// </summary>
		/// <param name="Trigger">The trigger to subscribe to</param>
		/// <param name="UserId">The user name</param>
		/// <param name="Email">Whether to receive email notifications</param>
		/// <param name="Slack">Whether to receive Slack notifications</param>
		/// <returns>The new trigger state</returns>
		Task<INotificationTrigger?> UpdateSubscriptionsAsync(INotificationTrigger Trigger, UserId UserId, bool? Email, bool? Slack);
	}
}
