// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
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
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Collection of lease documents
	/// </summary>
	public class LeaseCollection : ILeaseCollection
	{
		/// <summary>
		/// Concrete implementation of a lease document
		/// </summary>
		class LeaseDocument : ILease
		{
			public LeaseId Id { get; set; }
			public string Name { get; set; }
			public AgentId AgentId { get; set; }
			public ObjectId SessionId { get; set; }
			public StreamId? StreamId { get; set; }
			public PoolId? PoolId { get; set; }
			public LogId? LogId { get; set; }
			public DateTime StartTime { get; set; }
			public DateTime? FinishTime { get; set; }
			public byte[] Payload { get; set; }
			public LeaseOutcome Outcome { get; set; }

			[BsonIgnoreIfNull]
			public byte[]? Output { get; set; }

			ReadOnlyMemory<byte> ILease.Payload => Payload;
			ReadOnlyMemory<byte> ILease.Output => Output ?? Array.Empty<byte>();

			[BsonConstructor]
			private LeaseDocument()
			{
				Name = String.Empty;
				Payload = null!;
			}

			public LeaseDocument(LeaseId Id, string Name, AgentId AgentId, ObjectId SessionId, StreamId? StreamId, PoolId? PoolId, LogId? LogId, DateTime StartTime, byte[] Payload)
			{
				this.Id = Id;
				this.Name = Name;
				this.AgentId = AgentId;
				this.SessionId = SessionId;
				this.StreamId = StreamId;
				this.PoolId = PoolId;
				this.LogId = LogId;
				this.StartTime = StartTime;
				this.Payload = Payload;
			}
		}

		/// <summary>
		/// Collection of lease documents
		/// </summary>
		readonly IMongoCollection<LeaseDocument> Leases;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		public LeaseCollection(DatabaseService DatabaseService)
		{
			Leases = DatabaseService.GetCollection<LeaseDocument>("Leases");

			if (!DatabaseService.ReadOnlyMode)
			{
				Leases.Indexes.CreateOne(new CreateIndexModel<LeaseDocument>(Builders<LeaseDocument>.IndexKeys.Ascending(x => x.AgentId)));
				Leases.Indexes.CreateOne(new CreateIndexModel<LeaseDocument>(Builders<LeaseDocument>.IndexKeys.Ascending(x => x.SessionId)));
				Leases.Indexes.CreateOne(new CreateIndexModel<LeaseDocument>(Builders<LeaseDocument>.IndexKeys.Ascending(x => x.StartTime)));
				Leases.Indexes.CreateOne(new CreateIndexModel<LeaseDocument>(Builders<LeaseDocument>.IndexKeys.Ascending(x => x.FinishTime)));
			}
		}

		/// <inheritdoc/>
		public async Task<ILease> AddAsync(LeaseId Id, string Name, AgentId AgentId, ObjectId SessionId, StreamId? StreamId, PoolId? PoolId, LogId? LogId, DateTime StartTime, byte[] Payload)
		{
			LeaseDocument Lease = new LeaseDocument(Id, Name, AgentId, SessionId, StreamId, PoolId, LogId, StartTime, Payload);
			await Leases.ReplaceOneAsync(x => x.Id == Id, Lease, new ReplaceOptions { IsUpsert = true });
			return Lease;
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(LeaseId LeaseId)
		{
			await Leases.DeleteOneAsync(x => x.Id == LeaseId);
		}

		/// <inheritdoc/>
		public async Task<ILease?> GetAsync(LeaseId LeaseId)
		{
			return await Leases.Find(x => x.Id == LeaseId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<ILease>> FindLeasesAsync(AgentId? AgentId, ObjectId? SessionId, DateTime? MinTime, DateTime? MaxTime, int? Index, int? Count)
		{
			FilterDefinitionBuilder<LeaseDocument> FilterBuilder = Builders<LeaseDocument>.Filter;

			FilterDefinition<LeaseDocument> Filter = FilterDefinition<LeaseDocument>.Empty;
			if (AgentId != null)
			{
				Filter &= FilterBuilder.Eq(x => x.AgentId, AgentId.Value);
			}
			if (SessionId != null)
			{
				Filter &= FilterBuilder.Eq(x => x.SessionId, SessionId.Value);
			}
			if (MinTime != null)
			{
				Filter &= FilterBuilder.Or(FilterBuilder.Eq(x => x.FinishTime!, null), FilterBuilder.Gt(x => x.FinishTime!, MinTime.Value));
			}
			if (MaxTime != null)
			{
				Filter &= FilterBuilder.Lt(x => x.StartTime, MaxTime.Value);
			}

			List<LeaseDocument> Results = await Leases.Find(Filter).SortByDescending(x => x.StartTime).Range(Index, Count).ToListAsync();
			return Results.ConvertAll<ILease>(x => x);
		}

		/// <inheritdoc/>
		public async Task<List<ILease>> FindActiveLeasesAsync(int? Index = null, int? Count = null)
		{
			List<LeaseDocument> Results = await Leases.Find(x => x.FinishTime == null).Range(Index, Count).ToListAsync();
			return Results.ConvertAll<ILease>(x => x);
		}

		/// <inheritdoc/>
		public async Task<bool> TrySetOutcomeAsync(LeaseId LeaseId, DateTime FinishTime, LeaseOutcome Outcome, byte[]? Output)
		{
			FilterDefinitionBuilder<LeaseDocument> FilterBuilder = Builders<LeaseDocument>.Filter;
			FilterDefinition<LeaseDocument> Filter = FilterBuilder.Eq(x => x.Id, LeaseId) & FilterBuilder.Eq(x => x.FinishTime, null);

			UpdateDefinitionBuilder<LeaseDocument> UpdateBuilder = Builders<LeaseDocument>.Update;
			UpdateDefinition<LeaseDocument> Update = UpdateBuilder.Set(x => x.FinishTime, FinishTime).Set(x => x.Outcome, Outcome);

			if(Output != null && Output.Length > 0)
			{
				Update = Update.Set(x => x.Output, Output);
			}

			UpdateResult Result = await Leases.UpdateOneAsync(Filter, Update);
			return Result.ModifiedCount > 0;
		}
	}
}
