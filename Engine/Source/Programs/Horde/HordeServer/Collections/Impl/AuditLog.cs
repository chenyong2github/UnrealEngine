// Copyright Epic Games, Inc. All Rights Reserved.

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
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Collection of agent documents
	/// </summary>
	class AuditLog<TTargetId> : IAuditLog<TTargetId>
	{
		class AuditLogMessage : IAuditLogMessage<TTargetId>
		{
			public ObjectId Id { get; set; }

			[BsonElement("tid")]
			public TTargetId TargetId { get; set; }

			[BsonElement("t")]
			public DateTime TimeUtc { get; set; }

			[BsonElement("l")]
			public LogLevel Level { get; set; }

			[BsonElement("d")]
			public string Data { get; set; }

			public AuditLogMessage()
			{
				TargetId = default!;
				Data = String.Empty;
			}

			public AuditLogMessage(IAuditLogMessage<TTargetId> Other)
			{
				this.Id = ObjectId.GenerateNewId();
				this.TargetId = Other.TargetId;
				this.TimeUtc = Other.TimeUtc;
				this.Level = Other.Level;
				this.Data = Other.Data;
			}
		}

		IMongoCollection<AuditLogMessage> Messages;

		public AuditLog(DatabaseService DatabaseService, string CollectionName)
		{
			Messages = DatabaseService.GetCollection<AuditLogMessage>(CollectionName);
			if (!DatabaseService.ReadOnlyMode)
			{
				Messages.Indexes.CreateOne(new CreateIndexModel<AuditLogMessage>(Builders<AuditLogMessage>.IndexKeys.Ascending(x => x.Id).Descending(x => x.TimeUtc)));
			}
		}

		public Task AddAsync(NewAuditLogMessage<TTargetId> NewMessage)
		{
			AuditLogMessage Message = new AuditLogMessage(NewMessage);
			return Messages.InsertOneAsync(Message);
		}

		public async IAsyncEnumerable<IAuditLogMessage<TTargetId>> FindAsync(TTargetId Id, DateTime? MinTime = null, DateTime? MaxTime = null, int? Index = null, int? Count = null)
		{
			FilterDefinition<AuditLogMessage> Filter = Builders<AuditLogMessage>.Filter.Eq(x => x.TargetId, Id);
			if (MinTime != null)
			{
				Filter &= Builders<AuditLogMessage>.Filter.Gte(x => x.TimeUtc, MinTime.Value);
			}
			if (MaxTime != null)
			{
				Filter &= Builders<AuditLogMessage>.Filter.Lte(x => x.TimeUtc, MaxTime.Value);
			}

			using (IAsyncCursor<AuditLogMessage> Cursor = await Messages.Find(Filter).SortByDescending(x => x.TimeUtc).ToCursorAsync())
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

		public async Task<long> DeleteAsync(TTargetId Id, DateTime? MinTime = null, DateTime? MaxTime = null)
		{
			FilterDefinition<AuditLogMessage> Filter = Builders<AuditLogMessage>.Filter.Eq(x => x.TargetId, Id);
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

	/// <summary>
	/// Factory for instantiating audit log instances
	/// </summary>
	/// <typeparam name="TTargetId"></typeparam>
	public class AuditLogFactory<TTargetId> : IAuditLogFactory<TTargetId>
	{
		DatabaseService DatabaseService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		public AuditLogFactory(DatabaseService DatabaseService)
		{
			this.DatabaseService = DatabaseService;
		}

		/// <inheritdoc/>
		public IAuditLog<TTargetId> Create(string Name)
		{
			return new AuditLog<TTargetId>(DatabaseService, Name);
		}
	}
}
