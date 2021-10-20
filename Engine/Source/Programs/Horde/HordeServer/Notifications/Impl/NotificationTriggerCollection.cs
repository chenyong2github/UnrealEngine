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

namespace HordeServer.Collections.Impl
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Collection of notification triggers
	/// </summary>
	public class NotificationTriggerCollection : INotificationTriggerCollection
	{
		class SubscriptionDocument : INotificationSubscription
		{
			[BsonIgnoreIfNull]
			public string? User { get; set; }

			public UserId UserId { get; set; }
			public bool Email { get; set; }
			public bool Slack { get; set; }

			[BsonConstructor]
			private SubscriptionDocument()
			{
				User = null!;
			}

			public SubscriptionDocument(UserId UserId, bool Email, bool Slack)
			{
				this.UserId = UserId;
				this.Email = Email;
				this.Slack = Slack;
			}

			public SubscriptionDocument(INotificationSubscription Subscription)
			{
				this.UserId = Subscription.UserId;
				this.Email = Subscription.Email;
				this.Slack = Subscription.Slack;
			}
		}

		class TriggerDocument : INotificationTrigger
		{
			public ObjectId Id { get; set; }
			public bool Fired { get; set; }
			public List<SubscriptionDocument> Subscriptions { get; set; } = new List<SubscriptionDocument>();
			public int UpdateIndex { get; set; }

			IReadOnlyList<INotificationSubscription> INotificationTrigger.Subscriptions => Subscriptions;
		}

		IMongoCollection<TriggerDocument> Triggers;
		IUserCollection UserCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database singleton</param>
		/// <param name="UserCollection"></param>
		public NotificationTriggerCollection(DatabaseService DatabaseService, IUserCollection UserCollection)
		{
			this.Triggers = DatabaseService.GetCollection<TriggerDocument>("NotificationTriggers");
			this.UserCollection = UserCollection;
		}

		/// <inheritdoc/>
		public async Task<INotificationTrigger?> GetAsync(ObjectId TriggerId)
		{
			TriggerDocument? Trigger = await Triggers.Find(x => x.Id == TriggerId).FirstOrDefaultAsync();
			if (Trigger != null)
			{
				for (int Idx = 0; Idx < Trigger.Subscriptions.Count; Idx++)
				{
					SubscriptionDocument Subscription = Trigger.Subscriptions[Idx];
					if (Subscription.User != null)
					{
						IUser? User = await UserCollection.FindUserByLoginAsync(Subscription.User);
						if (User == null)
						{
							Trigger.Subscriptions.RemoveAt(Idx);
							Idx--;
						}
						else
						{
							Subscription.UserId = User.Id;
							Subscription.User = null;
						}
					}
				}
			}
			return Trigger;
		}

		/// <inheritdoc/>
		public async Task<INotificationTrigger> FindOrAddAsync(ObjectId TriggerId)
		{
			for (; ; )
			{
				// Find an existing trigger
				INotificationTrigger? Existing = await GetAsync(TriggerId);
				if (Existing != null)
				{
					return Existing;
				}

				// Try to insert a new document
				try
				{
					TriggerDocument NewDocument = new TriggerDocument();
					NewDocument.Id = TriggerId;
					await Triggers.InsertOneAsync(NewDocument);
					return NewDocument;
				}
				catch (MongoWriteException Ex)
				{
					if (Ex.WriteError.Category != ServerErrorCategory.DuplicateKey)
					{
						throw;
					}
				}
			}
		}

		/// <summary>
		/// Updates an existing document
		/// </summary>
		/// <param name="Trigger">The trigger to update</param>
		/// <param name="Transaction">The update definition</param>
		/// <returns>The updated document</returns>
		async Task<INotificationTrigger?> TryUpdateAsync(INotificationTrigger Trigger, TransactionBuilder<TriggerDocument> Transaction)
		{
			TriggerDocument Document = (TriggerDocument)Trigger;
			int NextUpdateIndex = Document.UpdateIndex + 1;

			FilterDefinition<TriggerDocument> Filter = Builders<TriggerDocument>.Filter.Expr(x => x.Id == Trigger.Id && x.UpdateIndex == Document.UpdateIndex);
			UpdateDefinition<TriggerDocument> Update = Transaction.ToUpdateDefinition().Set(x => x.UpdateIndex, NextUpdateIndex);

			UpdateResult Result = await Triggers.UpdateOneAsync(Filter, Update);
			if (Result.ModifiedCount > 0)
			{
				Transaction.ApplyTo(Document);
				Document.UpdateIndex = NextUpdateIndex;
				return Document;
			}

			return null;
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(ObjectId TriggerId)
		{
			await Triggers.DeleteOneAsync(x => x.Id == TriggerId);
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(List<ObjectId> TriggerIds)
		{
			FilterDefinition<TriggerDocument> Filter = Builders<TriggerDocument>.Filter.In(x => x.Id, TriggerIds);
			await Triggers.DeleteManyAsync(Filter);
		}

		/// <inheritdoc/>
		public async Task<INotificationTrigger?> FireAsync(INotificationTrigger Trigger)
		{
			if (Trigger.Fired)
			{
				return null;
			}

			for (; ; )
			{
				TransactionBuilder<TriggerDocument> Transaction = new TransactionBuilder<TriggerDocument>();
				Transaction.Set(x => x.Fired, true);

				INotificationTrigger? NewTrigger = await TryUpdateAsync(Trigger, Transaction);
				if (NewTrigger != null)
				{
					return NewTrigger;
				}

				NewTrigger = await FindOrAddAsync(Trigger.Id); // Need to add to prevent race condition on triggering vs adding
				if (NewTrigger == null || NewTrigger.Fired)
				{
					return null;
				}
			}
		}

		/// <inheritdoc/>
		public async Task<INotificationTrigger?> UpdateSubscriptionsAsync(INotificationTrigger Trigger, UserId UserId, bool? Email, bool? Slack)
		{
			for (; ; )
			{
				// If the trigger has already fired, don't add a new subscription to it
				if(Trigger.Fired)
				{
					return Trigger;
				}

				// Try to update the trigger
				List<SubscriptionDocument> NewSubscriptions = new List<SubscriptionDocument>();
				NewSubscriptions.AddRange(Trigger.Subscriptions.Select(x => new SubscriptionDocument(x)));

				SubscriptionDocument? NewSubscription = NewSubscriptions.FirstOrDefault(x => x.UserId == UserId);
				if (NewSubscription == null)
				{
					NewSubscription = new SubscriptionDocument(UserId, Email ?? false, Slack ?? false);
					NewSubscriptions.Add(NewSubscription);
				}
				else
				{
					NewSubscription.Email = Email ?? NewSubscription.Email;
					NewSubscription.Slack = Slack ?? NewSubscription.Slack;
				}

				TransactionBuilder<TriggerDocument> Transaction = new TransactionBuilder<TriggerDocument>();
				Transaction.Set(x => x.Subscriptions, NewSubscriptions);

				INotificationTrigger? NewTrigger = await TryUpdateAsync(Trigger, Transaction);
				if (NewTrigger != null)
				{
					return NewTrigger;
				}

				NewTrigger = await GetAsync(Trigger.Id);
				if (NewTrigger == null)
				{
					return null;
				}
			}
		}
	}
}
