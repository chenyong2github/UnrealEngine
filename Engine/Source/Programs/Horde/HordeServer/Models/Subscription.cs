// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Api;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;

namespace HordeServer.Models
{
	/// <summary>
	/// The type of notification to send
	/// </summary>
	public enum NotificationType
	{
		/// <summary>
		/// Send a DM on Slack
		/// </summary>
		Slack,
	}

	/// <summary>
	/// Subscription to a type of event
	/// </summary>
	public interface ISubscription
	{
		/// <summary>
		/// Unique id for this subscription
		/// </summary>
		public string Id { get; }

		/// <summary>
		/// Name of the event to subscribe to
		/// </summary>
		public IEvent Event { get; }

		/// <summary>
		/// User to notify
		/// </summary>
		public ObjectId UserId { get; }

		/// <summary>
		/// Type of notification to receive
		/// </summary>
		public NotificationType NotificationType { get; }
	}
}
