// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Collections
{
	using NamespaceId = StringId<INamespace>;
	using BucketId = StringId<IBucket>;

	class BucketCollection : IBucketCollection
	{
		class BucketCombinedId
		{
			public NamespaceId NamespaceId { get; set; }
			public BucketId BucketId { get; set; }
		}

		class Bucket : IBucket
		{
			public BucketCombinedId Id { get; set; } = null!;

			NamespaceId IBucket.NamespaceId => Id.NamespaceId;
			BucketId IBucket.BucketId => Id.BucketId;

			public bool Deleted { get; set; }
		}

		IMongoCollection<Bucket> Buckets;

		public BucketCollection(DatabaseService DatabaseService)
		{
			this.Buckets = DatabaseService.GetCollection<Bucket>("Storage.Buckets");

			if (!DatabaseService.ReadOnlyMode)
			{
				this.Buckets.Indexes.CreateOne(new CreateIndexModel<Bucket>(Builders<Bucket>.IndexKeys.Ascending(x => x.Id.NamespaceId)));
			}
		}

		public async Task AddOrUpdateAsync(NamespaceId NamespaceId, BucketId BucketId, BucketConfig Bucket)
		{
			Bucket NewBucket = new Bucket();
			NewBucket.Id = new BucketCombinedId { NamespaceId = NamespaceId, BucketId = BucketId };
			await Buckets.ReplaceOneAsync(x => x.Id == NewBucket.Id, NewBucket, new ReplaceOptions { IsUpsert = true });
		}

		public async Task<List<IBucket>> FindAsync(NamespaceId NamespaceId)
		{
			return await Buckets.Find(x => x.Id.NamespaceId == NamespaceId).ToListAsync<Bucket, IBucket>();
		}

		public async Task<IBucket?> GetAsync(NamespaceId NamespaceId, BucketId BucketId)
		{
			BucketCombinedId Id = new BucketCombinedId { NamespaceId = NamespaceId, BucketId = BucketId };
			return await Buckets.Find(x => x.Id == Id).FirstOrDefaultAsync();
		}

		public async Task RemoveAsync(NamespaceId NamespaceId, BucketId BucketId)
		{
			BucketCombinedId Id = new BucketCombinedId { NamespaceId = NamespaceId, BucketId = BucketId };
			await Buckets.UpdateOneAsync(x => x.Id == Id, Builders<Bucket>.Update.Set(x => x.Deleted, true));
		}
	}
}
