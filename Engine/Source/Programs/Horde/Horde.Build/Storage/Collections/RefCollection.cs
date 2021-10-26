// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Collections
{
	using NamespaceId = StringId<INamespace>;
	using BucketId = StringId<IBucket>;

	class RefCollection : IRefCollection
	{
		class RefId
		{
			[BsonElement("ns")]
			public NamespaceId NamespaceId { get; set; }
	
			[BsonElement("bkt")]
			public BucketId BucketId { get; set; }

			[BsonElement("name")]
			public string Name { get; set; }

			[BsonConstructor]
			public RefId()
			{
				Name = null!;
			}

			public RefId(NamespaceId NamespaceId, BucketId BucketId, string Name)
			{
				this.NamespaceId = NamespaceId;
				this.BucketId = BucketId;
				this.Name = Name;
			}
		}

		class Ref : IRef
		{
			public RefId Id { get; set; } = null!;

			[BsonElement("t")]
			public DateTime LastAccessTime { get; set; }

			[BsonElement("d")]
			public byte[] Data { get; set; } = null!;

			[BsonElement("f")]
			public bool Finalized { get; set; }

			public NamespaceId NamespaceId => Id.NamespaceId;
			public BucketId BucketId => Id.BucketId;
			public string Name => Id.Name;

			public CbObject Value => new CbObject(Data);
		}

		IMongoCollection<Ref> Refs;
		IBlobCollection BlobCollection;
		IObjectCollection ObjectCollection;

		public RefCollection(DatabaseService DatabaseService, IBlobCollection BlobCollection, IObjectCollection ObjectCollection)
		{
			this.Refs = DatabaseService.GetCollection<Ref>("Storage.Refs");
			this.BlobCollection = BlobCollection;
			this.ObjectCollection = ObjectCollection;
		}

		public async Task<List<IoHash>> SetAsync(NamespaceId NamespaceId, BucketId BucketId, string Name, CbObject Value)
		{
			Ref NewRef = new Ref();
			NewRef.Id = new RefId(NamespaceId, BucketId, Name);
			NewRef.LastAccessTime = DateTime.UtcNow;
			NewRef.Data = Value.GetView().ToArray();
			await Refs.ReplaceOneAsync(x => x.Id == NewRef.Id, NewRef, new ReplaceOptions { IsUpsert = true });

			return await FinalizeAsync(NewRef);
		}

		public async Task<List<IoHash>> FinalizeAsync(IRef Ref)
		{
			HashSet<IoHash> MissingHashes = new HashSet<IoHash>();
			if (!Ref.Finalized)
			{
				HashSet<IoHash> CheckedObjectHashes = new HashSet<IoHash>();
				HashSet<IoHash> CheckedBinaryHashes = new HashSet<IoHash>();
				await FinalizeInternalAsync(Ref.NamespaceId, Ref.Value, CheckedObjectHashes, CheckedBinaryHashes, MissingHashes);

				if (MissingHashes.Count == 0)
				{
					RefId RefId = new RefId(Ref.NamespaceId, Ref.BucketId, Ref.Name);
					await Refs.UpdateOneAsync(x => x.Id == RefId, Builders<Ref>.Update.Set(x => x.Finalized, true));
				}
			}
			return MissingHashes.ToList();
		}

		async Task FinalizeInternalAsync(NamespaceId NamespaceId, CbObject Object, HashSet<IoHash> CheckedObjectHashes, HashSet<IoHash> CheckedBinaryHashes, HashSet<IoHash> MissingHashes)
		{
			List<IoHash> NewObjectHashes = new List<IoHash>();
			List<IoHash> NewBinaryHashes = new List<IoHash>();
			Object.IterateAttachments(Field =>
			{
				IoHash AttachmentHash = Field.AsAttachment();
				if (Field.IsObjectAttachment())
				{
					if (CheckedObjectHashes.Add(AttachmentHash))
					{
						NewObjectHashes.Add(AttachmentHash);
					}
				}
				else
				{
					if (CheckedBinaryHashes.Add(AttachmentHash))
					{
						NewBinaryHashes.Add(AttachmentHash);
					}
				}
			});

			foreach (IoHash NewHash in NewObjectHashes)
			{
				CbObject? NextObject = await ObjectCollection.GetAsync(NamespaceId, NewHash);
				if (NextObject == null)
				{
					MissingHashes.Add(NewHash);
				}
				else
				{
					await FinalizeInternalAsync(NamespaceId, NextObject, CheckedObjectHashes, CheckedBinaryHashes, MissingHashes);
				}
			}

			if (NewBinaryHashes.Count > 0)
			{
				MissingHashes.UnionWith(NewBinaryHashes.Except(await BlobCollection.ExistsAsync(NamespaceId, NewBinaryHashes)));
			}
		}

		public async Task<IRef?> GetAsync(NamespaceId NamespaceId, BucketId BucketId, string Name)
		{
			RefId RefId = new RefId(NamespaceId, BucketId, Name);
			return await Refs.FindOneAndUpdateAsync<Ref>(x => x.Id == RefId, Builders<Ref>.Update.Set(x => x.LastAccessTime, DateTime.UtcNow), new FindOneAndUpdateOptions<Ref> { ReturnDocument = ReturnDocument.After });
		}

		public async Task<bool> TouchAsync(NamespaceId NamespaceId, BucketId BucketId, string Name)
		{
			RefId RefId = new RefId(NamespaceId, BucketId, Name);
			UpdateResult Result = await Refs.UpdateOneAsync(x => x.Id == RefId, Builders<Ref>.Update.Set(x => x.LastAccessTime, DateTime.UtcNow));
			return Result.MatchedCount > 0;
		}

		public async Task<bool> DeleteAsync(NamespaceId NamespaceId, BucketId BucketId, string Name)
		{
			RefId RefId = new RefId(NamespaceId, BucketId, Name);
			DeleteResult Result = await Refs.DeleteOneAsync(x => x.Id == RefId);
			return Result.DeletedCount > 0;
		}
	}
}
