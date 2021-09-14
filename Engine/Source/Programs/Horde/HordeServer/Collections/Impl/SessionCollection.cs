// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.DependencyInjection;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Collection of session documents
	/// </summary>
	public class SessionCollection : ISessionCollection
	{
		/// <summary>
		/// Concrete implementation of ISession
		/// </summary>
		class SessionDocument : ISession
		{
			[BsonRequired, BsonId]
			public ObjectId Id { get; set; }

			[BsonRequired]
			public AgentId AgentId { get; set; }

			public DateTime StartTime { get; set; }
			public DateTime? FinishTime { get; set; }
			public List<string>? Properties { get; set; }
			public Dictionary<string, int>? Resources { get; set; }
			public string? Version { get; set; }

			IReadOnlyList<string>? ISession.Properties => Properties;

			[BsonConstructor]
			private SessionDocument()
			{
			}

			public SessionDocument(ObjectId Id, AgentId AgentId, DateTime StartTime, IReadOnlyList<string>? Properties, IReadOnlyDictionary<string, int>? Resources, string? Version)
			{
				this.Id = Id;
				this.AgentId = AgentId;
				this.StartTime = StartTime;
				if (Properties != null)
				{
					this.Properties = new List<string>(Properties);
				}
				if (Resources != null)
				{
					this.Resources = new Dictionary<string, int>(Resources);
				}
				this.Version = Version;
			}
		}

		/// <summary>
		/// Collection of session documents
		/// </summary>
		readonly IMongoCollection<SessionDocument> Sessions;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service</param>
		public SessionCollection(DatabaseService DatabaseService)
		{
			Sessions = DatabaseService.GetCollection<SessionDocument>("Sessions");

			if (!DatabaseService.ReadOnlyMode)
			{
				Sessions.Indexes.CreateOne(new CreateIndexModel<SessionDocument>(Builders<SessionDocument>.IndexKeys.Ascending(x => x.AgentId)));
				Sessions.Indexes.CreateOne(new CreateIndexModel<SessionDocument>(Builders<SessionDocument>.IndexKeys.Ascending(x => x.StartTime)));
				Sessions.Indexes.CreateOne(new CreateIndexModel<SessionDocument>(Builders<SessionDocument>.IndexKeys.Ascending(x => x.FinishTime)));
			}
		}

		/// <inheritdoc/>
		public async Task<ISession> AddAsync(ObjectId Id, AgentId AgentId, DateTime StartTime, IReadOnlyList<string>? Properties, IReadOnlyDictionary<string, int>? Resources, string? Version)
		{
			SessionDocument NewSession = new SessionDocument(Id, AgentId, StartTime, Properties, Resources, Version);
			await Sessions.InsertOneAsync(NewSession);
			return NewSession;
		}

		/// <inheritdoc/>
		public async Task<ISession?> GetAsync(ObjectId SessionId)
		{
			return await Sessions.Find(x => x.Id == SessionId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<ISession>> FindAsync(AgentId AgentId, DateTime? StartTime, DateTime? FinishTime, int Index, int Count)
		{
			FilterDefinitionBuilder<SessionDocument> FilterBuilder = Builders<SessionDocument>.Filter;

			FilterDefinition<SessionDocument> Filter = FilterBuilder.Eq(x => x.AgentId, AgentId);
			if (StartTime != null)
			{
				Filter &= FilterBuilder.Gte(x => x.StartTime, StartTime.Value);
			}
			if (FinishTime != null)
			{
				Filter &= FilterBuilder.Or(FilterBuilder.Eq(x => x.FinishTime, null), FilterBuilder.Lte(x => x.FinishTime, FinishTime.Value));
			}

			List<SessionDocument> Results = await Sessions.Find(Filter).SortByDescending(x => x.StartTime).Skip(Index).Limit(Count).ToListAsync();
			return Results.ConvertAll<ISession>(x => x);
		}

		/// <inheritdoc/>
		public async Task<List<ISession>> FindActiveSessionsAsync(int? Index, int? Count)
		{
			List<SessionDocument> Results = await Sessions.Find(x => x.FinishTime == null).Range(Index, Count).ToListAsync();
			return Results.ConvertAll<ISession>(x => x);
		}

		/// <inheritdoc/>
		public Task UpdateAsync(ObjectId SessionId, DateTime FinishTime, IReadOnlyList<string> Properties, IReadOnlyDictionary<string, int> Resources)
		{
			UpdateDefinition<SessionDocument> Update = Builders<SessionDocument>.Update
				.Set(x => x.FinishTime, FinishTime)
				.Set(x => x.Properties, new List<string>(Properties))
				.Set(x => x.Resources, new Dictionary<string, int>(Resources));

			return Sessions.FindOneAndUpdateAsync(x => x.Id == SessionId, Update);
		}

		/// <inheritdoc/>
		public Task DeleteAsync(ObjectId SessionId)
		{
			return Sessions.DeleteOneAsync(x => x.Id == SessionId);
		}
	}
}
