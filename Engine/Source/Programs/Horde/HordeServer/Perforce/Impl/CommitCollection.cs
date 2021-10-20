// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using MongoDB.Bson.Serialization.Attributes;

namespace HordeServer.Commits.Impl
{
	using StreamId = StringId<IStream>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Concrete implementation of ICommitCollection
	/// </summary>
	class CommitCollection : ICommitCollection
	{
		class Commit : ICommit
		{
			[BsonIgnoreIfDefault]
			public ObjectId Id { get; set; }

			[BsonElement("s")]
			public StreamId StreamId { get; set; }

			[BsonElement("c")]
			public int Change { get; set; }

			[BsonElement("oc"), BsonIgnoreIfNull]
			public int? OriginalChange { get; set; }

			[BsonElement("a")]
			public UserId AuthorId { get; set; }

			[BsonElement("o"), BsonIgnoreIfNull]
			public UserId? OwnerId { get; set; }

			[BsonElement("d")]
			public string Description { get; set; } = String.Empty;

			[BsonElement("p")]
			public string BasePath { get; set; } = String.Empty;

			[BsonElement("tr")]
			public string? TreeRef { get; set; }

			[BsonElement("t")]
			public DateTime DateUtc { get; set; }

			int ICommit.OriginalChange => OriginalChange ?? Change;
			UserId ICommit.OwnerId => OwnerId ?? AuthorId;

			public Commit()
			{
			}

			public Commit(NewCommit NewCommit)
			{
				this.Change = NewCommit.Change;
				if (NewCommit.OriginalChange != NewCommit.Change)
				{
					this.OriginalChange = NewCommit.OriginalChange;
				}
				this.StreamId = NewCommit.StreamId;
				this.AuthorId = NewCommit.AuthorId;
				if (NewCommit.OwnerId != NewCommit.AuthorId)
				{
					this.OwnerId = NewCommit.OwnerId;
				}
				this.Description = NewCommit.Description;
				this.BasePath = NewCommit.BasePath;
				this.TreeRef = NewCommit.TreeRef;
				this.DateUtc = NewCommit.DateUtc;
			}
		}

		IMongoCollection<Commit> Commits;

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitCollection(DatabaseService DatabaseService)
		{
			Commits = DatabaseService.Database.GetCollection<Commit>("Commits");

			if (!DatabaseService.ReadOnlyMode)
			{
				Commits.Indexes.CreateOne(new CreateIndexModel<Commit>(Builders<Commit>.IndexKeys.Ascending(x => x.StreamId).Descending(x => x.Change), new CreateIndexOptions { Unique = true }));
			}
		}

		/// <inheritdoc/>
		public async Task<ICommit> AddOrReplaceAsync(NewCommit NewCommit)
		{
			Commit Commit = new Commit(NewCommit);
			FilterDefinition<Commit> Filter = Builders<Commit>.Filter.Expr(x => x.StreamId == NewCommit.StreamId && x.Change == NewCommit.Change);
			return await Commits.FindOneAndReplaceAsync(Filter, new Commit(NewCommit), new FindOneAndReplaceOptions<Commit> { IsUpsert = true, ReturnDocument = ReturnDocument.After });
		}

		/// <inheritdoc/>
		public async Task<ICommit?> GetCommitAsync(ObjectId Id)
		{
			return await Commits.Find(x => x.Id == Id).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<ICommit>> FindCommitsAsync(StreamId StreamId, int? MinChange = null, int? MaxChange = null, int? Index = null, int? Count = null)
		{
			FilterDefinition<Commit> Filter = Builders<Commit>.Filter.Eq(x => x.StreamId, StreamId);
			if (MaxChange != null)
			{
				Filter &= Builders<Commit>.Filter.Lte(x => x.Change, MaxChange.Value);
			}
			if (MinChange != null)
			{
				Filter &= Builders<Commit>.Filter.Lte(x => x.Change, MinChange.Value);
			}
			return await Commits.Find(Filter).SortByDescending(x => x.Change).Range(Index, Count).ToListAsync<Commit, ICommit>();
		}
	}
}
