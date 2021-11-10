// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Text.Json;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	class AuditLog<TSubject> : IAuditLog<TSubject>, IAsyncDisposable, IDisposable
	{
		class AuditLogMessage : IAuditLogMessage<TSubject>
		{
			public ObjectId Id { get; set; }

			[BsonElement("s")]
			public TSubject Subject { get; set; }

			[BsonElement("t")]
			public DateTime TimeUtc { get; set; }

			[BsonElement("l")]
			public LogLevel Level { get; set; }

			[BsonElement("d")]
			public string Data { get; set; }

			public AuditLogMessage()
			{
				Subject = default!;
				Data = String.Empty;
			}

			public AuditLogMessage(TSubject Subject, DateTime TimeUtc, LogLevel Level, string Data)
			{
				this.Id = ObjectId.GenerateNewId();
				this.Subject = Subject;
				this.TimeUtc = TimeUtc;
				this.Level = Level;
				this.Data = Data;
			}
		}

		class AuditLogChannel : IAuditLogChannel<TSubject>
		{
			sealed class Scope : IDisposable
			{
				public void Dispose() { }
			}

			public readonly AuditLog<TSubject> Outer;
			public TSubject Subject { get; }

			public AuditLogChannel(AuditLog<TSubject> Outer, TSubject Subject)
			{
				this.Outer = Outer;
				this.Subject = Subject;
			}

			public IDisposable BeginScope<TState>(TState state) => new Scope();

			public bool IsEnabled(LogLevel logLevel) => true;

			public void Log<TState>(LogLevel LogLevel, EventId EventId, TState State, Exception? Exception, Func<TState, Exception?, string> Formatter)
			{
				DateTime Time = DateTime.UtcNow;

				LogEvent Event = LogEvent.FromState(LogLevel, EventId, State, Exception, Formatter);
				string Data = Event.ToJson();
				AuditLogMessage Message = new AuditLogMessage(Subject, Time, LogLevel, Data);
				Outer.MessageChannel.Writer.TryWrite(Message);

				using (IDisposable _ = Outer.Logger.BeginScope($"Subject: {{{Outer.SubjectProperty}}}", Subject))
				{
					Outer.Logger.Log(LogLevel, EventId, State, Exception, Formatter);
				}
			}

			public IAsyncEnumerable<IAuditLogMessage> FindAsync(DateTime? MinTime, DateTime? MaxTime, int? Index, int? Count) => Outer.FindAsync(Subject, MinTime, MaxTime, Index, Count);

			public Task<long> DeleteAsync(DateTime? MinTime, DateTime? MaxTime) => Outer.DeleteAsync(Subject, MinTime, MaxTime);
		}

		IMongoCollection<AuditLogMessage> Messages;
		Channel<AuditLogMessage> MessageChannel;
		string SubjectProperty;
		ILogger Logger;
		Task BackgroundTask;

		public IAuditLogChannel<TSubject> this[TSubject Subject] => new AuditLogChannel(this, Subject);

		public AuditLog(DatabaseService DatabaseService, string CollectionName, string SubjectProperty, ILogger Logger)
		{
			this.Messages = DatabaseService.GetCollection<AuditLogMessage>(CollectionName);
			this.MessageChannel = Channel.CreateUnbounded<AuditLogMessage>();
			this.SubjectProperty = SubjectProperty;
			this.Logger = Logger;

			if (!DatabaseService.ReadOnlyMode)
			{
				Messages.Indexes.CreateOne(new CreateIndexModel<AuditLogMessage>(Builders<AuditLogMessage>.IndexKeys.Ascending(x => x.Id).Descending(x => x.TimeUtc)));
			}

			BackgroundTask = Task.Run(() => WriteMessagesAsync());
		}

		public async ValueTask DisposeAsync()
		{
			MessageChannel.Writer.TryComplete();
			await BackgroundTask;
		}

		public void Dispose()
		{
			DisposeAsync().AsTask().Wait();
		}

		async Task WriteMessagesAsync()
		{
			while (await MessageChannel.Reader.WaitToReadAsync())
			{
				List<AuditLogMessage> NewMessages = new List<AuditLogMessage>();
				while (MessageChannel.Reader.TryRead(out AuditLogMessage? NewMessage))
				{
					NewMessages.Add(NewMessage);
				}
				if (NewMessages.Count > 0)
				{
					await Messages.InsertManyAsync(NewMessages);
				}
			}
		}

		async IAsyncEnumerable<IAuditLogMessage<TSubject>> FindAsync(TSubject Subject, DateTime? MinTime = null, DateTime? MaxTime = null, int? Index = null, int? Count = null)
		{
			FilterDefinition<AuditLogMessage> Filter = Builders<AuditLogMessage>.Filter.Eq(x => x.Subject, Subject);
			if (MinTime != null)
			{
				Filter &= Builders<AuditLogMessage>.Filter.Gte(x => x.TimeUtc, MinTime.Value);
			}
			if (MaxTime != null)
			{
				Filter &= Builders<AuditLogMessage>.Filter.Lte(x => x.TimeUtc, MaxTime.Value);
			}

			using (IAsyncCursor<AuditLogMessage> Cursor = await Messages.Find(Filter).SortByDescending(x => x.TimeUtc).Range(Index, Count).ToCursorAsync())
			{
				while (await Cursor.MoveNextAsync())
				{
					foreach (AuditLogMessage Message in Cursor.Current)
					{
						yield return Message;
					}
				}
			}
		}

		async Task<long> DeleteAsync(TSubject Subject, DateTime? MinTime = null, DateTime? MaxTime = null)
		{
			FilterDefinition<AuditLogMessage> Filter = Builders<AuditLogMessage>.Filter.Eq(x => x.Subject, Subject);
			if (MinTime != null)
			{
				Filter &= Builders<AuditLogMessage>.Filter.Gte(x => x.TimeUtc, MinTime.Value);
			}
			if (MaxTime != null)
			{
				Filter &= Builders<AuditLogMessage>.Filter.Lte(x => x.TimeUtc, MaxTime.Value);
			}

			DeleteResult Result = await Messages.DeleteManyAsync(Filter);
			return Result.DeletedCount;
		}
	}

	class AuditLogFactory<TSubject> : IAuditLogFactory<TSubject>
	{
		DatabaseService DatabaseService;
		ILogger<AuditLog<TSubject>> Logger;

		public AuditLogFactory(DatabaseService DatabaseService, ILogger<AuditLog<TSubject>> Logger)
		{
			this.DatabaseService = DatabaseService;
			this.Logger = Logger;
		}

		public IAuditLog<TSubject> Create(string CollectionName, string SubjectProperty)
		{
			return new AuditLog<TSubject>(DatabaseService, CollectionName, SubjectProperty, Logger);
		}
	}
}
