// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using Serilog.Events;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Collection of event documents
	/// </summary>
	public class LogEventCollection : ILogEventCollection
	{
		class LogEventId
		{
			[BsonElement("l")]
			public LogId LogId { get; set; }

			[BsonElement("n")]
			public int LineIndex { get; set; }
		}

		class LogEventDocument : ILogEvent
		{
			[BsonId]
			public LogEventId Id { get; set; }

			[BsonElement("w"), BsonIgnoreIfDefault, BsonDefaultValue(false)]
			public bool IsWarning { get; set; }

			[BsonElement("c"), BsonIgnoreIfNull]
			public int? LineCount { get; set; }

			[BsonElement("s")]
			public ObjectId? SpanId { get; set; }

			LogId ILogEvent.LogId => Id.LogId;
			int ILogEvent.LineIndex => Id.LineIndex;
			int ILogEvent.LineCount => LineCount ?? 1;
			EventSeverity ILogEvent.Severity => IsWarning ? EventSeverity.Warning : EventSeverity.Error;

			public LogEventDocument()
			{
				this.Id = new LogEventId();
			}

			public LogEventDocument(LogId LogId, EventSeverity Severity, int LineIndex, int LineCount, ObjectId? SpanId)
			{
				this.Id = new LogEventId { LogId = LogId, LineIndex = LineIndex };
				this.IsWarning = Severity == EventSeverity.Warning;
				this.LineCount = (LineCount > 1) ? (int?)LineCount : null;
				this.SpanId = SpanId;
			}

			public LogEventDocument(NewLogEventData Data)
				: this(Data.LogId, Data.Severity, Data.LineIndex, Data.LineCount, Data.SpanId)
			{
			}
		}

		class LegacyEventDocument
		{
			public ObjectId Id { get; set; }
			public DateTime Time { get; set; }
			public EventSeverity Severity { get; set; }
			public LogId LogId { get; set; }
			public int LineIndex { get; set; }
			public int LineCount { get; set; }

			public string? Message { get; set; }

			[BsonIgnoreIfNull, BsonElement("IssueId2")]
			public int? IssueId { get; set; }

			public BsonDocument? Data { get; set; }

			public int UpgradeVersion { get; set; }
		}

		/// <summary>
		/// Collection of event documents
		/// </summary>
		IMongoCollection<LogEventDocument> LogEvents;

		/// <summary>
		/// Collection of legacy event documents
		/// </summary>
		IMongoCollection<LegacyEventDocument> LegacyEvents;

		/// <summary>
		/// Logger for upgrade messages
		/// </summary>
		ILogger<LogEventCollection> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="Logger">The logger instance</param>
		public LogEventCollection(DatabaseService DatabaseService, ILogger<LogEventCollection> Logger)
		{
			this.Logger = Logger;

			LogEvents = DatabaseService.GetCollection<LogEventDocument>("LogEvents");
			LegacyEvents = DatabaseService.GetCollection<LegacyEventDocument>("Events");

			if (!DatabaseService.ReadOnlyMode)
			{
				LogEvents.Indexes.CreateOne(new CreateIndexModel<LogEventDocument>(Builders<LogEventDocument>.IndexKeys.Ascending(x => x.Id.LogId)));
				LogEvents.Indexes.CreateOne(new CreateIndexModel<LogEventDocument>(Builders<LogEventDocument>.IndexKeys.Ascending(x => x.SpanId).Ascending(x => x.Id)));
				LegacyEvents.Indexes.CreateOne(new CreateIndexModel<LegacyEventDocument>(Builders<LegacyEventDocument>.IndexKeys.Ascending(x => x.LogId)));
			}
		}

		/// <inheritdoc/>
		public Task AddAsync(NewLogEventData NewEvent)
		{
			return LogEvents.InsertOneAsync(new LogEventDocument(NewEvent));
		}

		/// <inheritdoc/>
		public Task AddManyAsync(List<NewLogEventData> NewEvents)
		{
			return LogEvents.InsertManyAsync(NewEvents.ConvertAll(x => new LogEventDocument(x)));
		}

		/// <inheritdoc/>
		public async Task<List<ILogEvent>> FindAsync(LogId LogId, int? Index = null, int? Count = null)
		{
			Logger.LogInformation("Querying for log events for log {LogId} creation time {CreateTime}", LogId, LogId.Value.CreationTime);

			FilterDefinitionBuilder<LogEventDocument> Builder = Builders<LogEventDocument>.Filter;

			FilterDefinition<LogEventDocument> Filter = Builder.Eq(x => x.Id.LogId, LogId);

			IFindFluent<LogEventDocument, LogEventDocument> Results = LogEvents.Find(Filter);
			if (Index != null)
			{
				Results = Results.Skip(Index.Value);
			}
			if (Count != null)
			{
				Results = Results.Limit(Count.Value);
			}

			List<LogEventDocument> EventDocuments = await Results.ToListAsync();
			return EventDocuments.ConvertAll<ILogEvent>(x => x);
		}

		/// <inheritdoc/>
		public async Task<List<ILogEvent>> FindEventsForSpanAsync(ObjectId SpanId, LogId[]? LogIds, int Index, int Count)
		{
			FilterDefinition<LogEventDocument> Filter = Builders<LogEventDocument>.Filter.Eq(x => x.SpanId, SpanId);
			if (LogIds != null && LogIds.Length > 0)
			{
				Filter &= Builders<LogEventDocument>.Filter.In(x => x.Id.LogId, LogIds);
			}

			List<LogEventDocument> EventDocuments = await LogEvents.Find(Filter).Skip(Index).Limit(Count).ToListAsync();
			return EventDocuments.ConvertAll<ILogEvent>(x => x);
		}

		/// <inheritdoc/>
		public async Task<List<ILogEvent>> FindEventsForSpansAsync(IEnumerable<ObjectId> SpanIds, LogId[]? LogIds, int Index, int Count)
		{
			FilterDefinition<LogEventDocument> Filter = Builders<LogEventDocument>.Filter.In(x => x.SpanId, SpanIds.Select<ObjectId, ObjectId?>(x => x));
			if (LogIds != null && LogIds.Length > 0)
			{
				Filter &= Builders<LogEventDocument>.Filter.In(x => x.Id.LogId, LogIds);
			}

			List<LogEventDocument> EventDocuments = await LogEvents.Find(Filter).Skip(Index).Limit(Count).ToListAsync();
			return EventDocuments.ConvertAll<ILogEvent>(x => x);
		}

		/// <inheritdoc/>
		public async Task DeleteLogAsync(LogId LogId)
		{
			await LogEvents.DeleteManyAsync(x => x.Id.LogId == LogId);
			await LegacyEvents.DeleteManyAsync(x => x.LogId == LogId);
		}

		/// <inheritdoc/>
		public async Task AddSpanToEventsAsync(IEnumerable<ILogEvent> Events, ObjectId SpanId)
		{
			FilterDefinition<LogEventDocument> EventFilter = Builders<LogEventDocument>.Filter.In(x => x.Id, Events.OfType<LogEventDocument>().Select(x => x.Id));
			UpdateDefinition<LogEventDocument> EventUpdate = Builders<LogEventDocument>.Update.Set(x => x.SpanId, SpanId);
			await LogEvents.UpdateManyAsync(EventFilter, EventUpdate);
		}
	}
}
