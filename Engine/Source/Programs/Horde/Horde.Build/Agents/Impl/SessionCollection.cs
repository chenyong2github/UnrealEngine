// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Horde.Build.Models;
using Horde.Build.Server;
using Horde.Build.Utilities;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;

namespace Horde.Build.Collections.Impl
{
	using SessionId = ObjectId<ISession>;

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
			public SessionId Id { get; set; }

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

			public SessionDocument(SessionId id, AgentId agentId, DateTime startTime, IReadOnlyList<string>? properties, IReadOnlyDictionary<string, int>? resources, string? version)
			{
				Id = id;
				AgentId = agentId;
				StartTime = startTime;
				if (properties != null)
				{
					Properties = new List<string>(properties);
				}
				if (resources != null)
				{
					Resources = new Dictionary<string, int>(resources);
				}
				Version = version;
			}
		}

		/// <summary>
		/// Collection of session documents
		/// </summary>
		readonly IMongoCollection<SessionDocument> _sessions;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="mongoService">The database service</param>
		public SessionCollection(MongoService mongoService)
		{
			_sessions = mongoService.GetCollection<SessionDocument>("Sessions");

			if (!mongoService.ReadOnlyMode)
			{
				_sessions.Indexes.CreateOne(new CreateIndexModel<SessionDocument>(Builders<SessionDocument>.IndexKeys.Ascending(x => x.AgentId)));
				_sessions.Indexes.CreateOne(new CreateIndexModel<SessionDocument>(Builders<SessionDocument>.IndexKeys.Ascending(x => x.StartTime)));
				_sessions.Indexes.CreateOne(new CreateIndexModel<SessionDocument>(Builders<SessionDocument>.IndexKeys.Ascending(x => x.FinishTime)));
			}
		}

		/// <inheritdoc/>
		public async Task<ISession> AddAsync(SessionId id, AgentId agentId, DateTime startTime, IReadOnlyList<string>? properties, IReadOnlyDictionary<string, int>? resources, string? version)
		{
			SessionDocument newSession = new SessionDocument(id, agentId, startTime, properties, resources, version);
			await _sessions.InsertOneAsync(newSession);
			return newSession;
		}

		/// <inheritdoc/>
		public async Task<ISession?> GetAsync(SessionId sessionId)
		{
			return await _sessions.Find(x => x.Id == sessionId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<ISession>> FindAsync(AgentId agentId, DateTime? startTime, DateTime? finishTime, int index, int count)
		{
			FilterDefinitionBuilder<SessionDocument> filterBuilder = Builders<SessionDocument>.Filter;

			FilterDefinition<SessionDocument> filter = filterBuilder.Eq(x => x.AgentId, agentId);
			if (startTime != null)
			{
				filter &= filterBuilder.Gte(x => x.StartTime, startTime.Value);
			}
			if (finishTime != null)
			{
				filter &= filterBuilder.Or(filterBuilder.Eq(x => x.FinishTime, null), filterBuilder.Lte(x => x.FinishTime, finishTime.Value));
			}

			List<SessionDocument> results = await _sessions.Find(filter).SortByDescending(x => x.StartTime).Skip(index).Limit(count).ToListAsync();
			return results.ConvertAll<ISession>(x => x);
		}

		/// <inheritdoc/>
		public async Task<List<ISession>> FindActiveSessionsAsync(int? index, int? count)
		{
			List<SessionDocument> results = await _sessions.Find(x => x.FinishTime == null).Range(index, count).ToListAsync();
			return results.ConvertAll<ISession>(x => x);
		}

		/// <inheritdoc/>
		public Task UpdateAsync(SessionId sessionId, DateTime finishTime, IReadOnlyList<string> properties, IReadOnlyDictionary<string, int> resources)
		{
			UpdateDefinition<SessionDocument> update = Builders<SessionDocument>.Update
				.Set(x => x.FinishTime, finishTime)
				.Set(x => x.Properties, new List<string>(properties))
				.Set(x => x.Resources, new Dictionary<string, int>(resources));

			return _sessions.FindOneAndUpdateAsync(x => x.Id == sessionId, update);
		}

		/// <inheritdoc/>
		public Task DeleteAsync(SessionId sessionId)
		{
			return _sessions.DeleteOneAsync(x => x.Id == sessionId);
		}
	}
}
