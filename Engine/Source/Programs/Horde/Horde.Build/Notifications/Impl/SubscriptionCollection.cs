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
using System.Globalization;
using System.Linq;
using System.Reflection.Metadata.Ecma335;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Collection of subscription documents
	/// </summary>
	public class SubscriptionCollection : ISubscriptionCollection
	{
		[BsonKnownTypes(typeof(JobCompleteEvent), typeof(LabelCompleteEvent), typeof(StepCompleteEvent))]
		abstract class Event : IEvent
		{
			public static Event FromRecord(EventRecord Record)
			{
				JobCompleteEventRecord? JobCompleteRecord = Record as JobCompleteEventRecord;
				if (JobCompleteRecord != null)
				{
					return new JobCompleteEvent(JobCompleteRecord);
				}

				LabelCompleteEventRecord? LabelCompleteRecord = Record as LabelCompleteEventRecord;
				if (LabelCompleteRecord != null)
				{
					return new LabelCompleteEvent(LabelCompleteRecord);
				}

				StepCompleteEventRecord? StepCompleteRecord = Record as StepCompleteEventRecord;
				if (StepCompleteRecord != null)
				{
					return new StepCompleteEvent(StepCompleteRecord);
				}

				throw new ArgumentException("Invalid record type");
			}

			public abstract EventRecord ToRecord();
		}

		class JobCompleteEvent : Event, IJobCompleteEvent
		{
			public StreamId StreamId { get; set; }
			public TemplateRefId TemplateId { get; set; }
			public LabelOutcome Outcome { get; set; }

			public JobCompleteEvent()
			{
			}

			public JobCompleteEvent(JobCompleteEventRecord Record)
			{
				StreamId = new StreamId(Record.StreamId);
				TemplateId = new TemplateRefId(Record.TemplateId);
				Outcome = Record.Outcome;
			}

			public override EventRecord ToRecord()
			{
				return new JobCompleteEventRecord(StreamId, TemplateId, Outcome);
			}

			public override string ToString()
			{
				return $"stream={StreamId}, template={TemplateId}, outcome={Outcome}";
			}
		}

		class LabelCompleteEvent : Event, ILabelCompleteEvent
		{
			public StreamId StreamId { get; set; }
			public TemplateRefId TemplateId { get; set; }
			public string? CategoryName { get; set; }
			public string LabelName { get; set; }
			public LabelOutcome Outcome { get; set; }

			public LabelCompleteEvent()
			{
				LabelName = String.Empty;
			}

			public LabelCompleteEvent(LabelCompleteEventRecord Record)
			{
				StreamId = new StreamId(Record.StreamId);
				TemplateId = new TemplateRefId(Record.TemplateId);
				CategoryName = Record.CategoryName;
				LabelName = Record.LabelName;
				Outcome = Record.Outcome;
			}

			public override EventRecord ToRecord()
			{
				return new LabelCompleteEventRecord(StreamId, TemplateId, CategoryName, LabelName, Outcome);
			}

			public override string ToString()
			{
				StringBuilder Result = new StringBuilder();
				Result.Append(CultureInfo.InvariantCulture, $"stream={StreamId}, template={TemplateId}");
				if(CategoryName != null)
				{
					Result.Append(CultureInfo.InvariantCulture, $", category={CategoryName}");
				}
				Result.Append(CultureInfo.InvariantCulture, $", label={LabelName}, outcome={Outcome}");
				return Result.ToString();
			}
		}

		class StepCompleteEvent : Event, IStepCompleteEvent
		{
			public StreamId StreamId { get; set; }
			public TemplateRefId TemplateId { get; set; }
			public string StepName { get; set; }
			public JobStepOutcome Outcome { get; set; }

			public StepCompleteEvent()
			{
				StepName = String.Empty;
			}

			public StepCompleteEvent(StepCompleteEventRecord Record)
			{
				StreamId = new StreamId(Record.StreamId);
				TemplateId = new TemplateRefId(Record.TemplateId);
				StepName = Record.StepName;
				Outcome = Record.Outcome;
			}

			public override EventRecord ToRecord()
			{
				return new StepCompleteEventRecord(StreamId, TemplateId, StepName, Outcome);
			}

			public override string ToString()
			{
				return $"stream={StreamId}, template={TemplateId}, step={StepName}, outcome={Outcome}";
			}
		}

		class Subscription : ISubscription
		{
			public string Id { get; set; }
			public Event Event { get; set; }
			public UserId UserId { get; set; }
			public NotificationType NotificationType { get; set; }

			IEvent ISubscription.Event => Event;

			[BsonConstructor]
			private Subscription()
			{
				this.Id = String.Empty;
				this.Event = null!;
			}

			public Subscription(NewSubscription Subscription)
			{
				this.Event = Event.FromRecord(Subscription.Event);
				this.UserId = Subscription.UserId;
				this.NotificationType = Subscription.NotificationType;
				this.Id = ContentHash.SHA1($"{Event}, user={UserId}, type={NotificationType.ToString()}").ToString();
			}
		}

		IMongoCollection<Subscription> Collection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service</param>
		public SubscriptionCollection(DatabaseService DatabaseService)
		{
			Collection = DatabaseService.GetCollection<Subscription>("SubscriptionsV2");
			if (!DatabaseService.ReadOnlyMode)
			{
				Collection.Indexes.CreateOne(new CreateIndexModel<Subscription>(Builders<Subscription>.IndexKeys.Ascending(x => x.Event)));
				Collection.Indexes.CreateOne(new CreateIndexModel<Subscription>(Builders<Subscription>.IndexKeys.Ascending(x => x.UserId)));
			}
		}

		/// <inheritdoc/>
		public async Task<List<ISubscription>> AddAsync(IEnumerable<NewSubscription> NewSubscriptions)
		{
			List<Subscription> NewDocuments = NewSubscriptions.Select(x => new Subscription(x)).ToList();
			try
			{
				await Collection.InsertManyAsync(NewDocuments, new InsertManyOptions { IsOrdered = false });
			}
			catch (MongoBulkWriteException Ex)
			{
				foreach (WriteError WriteError in Ex.WriteErrors)
				{
					if (WriteError.Category != ServerErrorCategory.DuplicateKey)
					{
						throw;
					}
				}
			}
			return NewDocuments.ConvertAll<ISubscription>(x => x);
		}

		/// <inheritdoc/>
		public async Task RemoveAsync(IEnumerable<ISubscription> Subscriptions)
		{
			FilterDefinition<Subscription> Filter = Builders<Subscription>.Filter.In(x => x.Id, Subscriptions.Select(x => ((Subscription)x).Id));
			await Collection.DeleteManyAsync(Filter);
		}

		/// <inheritdoc/>
		public async Task<ISubscription?> GetAsync(string SubscriptionId)
		{
			FilterDefinition<Subscription> Filter = Builders<Subscription>.Filter.Eq(x => x.Id, SubscriptionId);
			return await Collection.Find(Filter).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<ISubscription>> FindSubscribersAsync(EventRecord EventRecord)
		{
			FilterDefinition<Subscription> Filter = Builders<Subscription>.Filter.Eq(x => x.Event, Event.FromRecord(EventRecord));
			List<Subscription> Results = await Collection.Find(Filter).ToListAsync();
			return Results.ConvertAll<ISubscription>(x => x);
		}

		/// <inheritdoc/>
		public async Task<List<ISubscription>> FindSubscriptionsAsync(UserId UserId)
		{
			List<Subscription> Results = await Collection.Find(x => x.UserId == UserId).ToListAsync();
			return Results.ConvertAll<ISubscription>(x => x);
		}
	}
}
