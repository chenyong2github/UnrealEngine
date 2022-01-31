// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using HordeServer.Services;
using HordeServer.Storage;
using HordeServer.Utilities;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace Horde.Build.Storage.Services
{
	using IRef = EpicGames.Horde.Storage.IRef;
	using BlobNotFoundException = EpicGames.Horde.Storage.BlobNotFoundException;

	/// <summary>
	/// Simple implementation of <see cref="IStorageClient"/> which uses the current <see cref="IStorageBackend"/> implementation without garbage collection.
	/// </summary>
	public class BasicStorageClient : IStorageClient
	{
		class RefImpl : IRef
		{
			public string Id { get; set; } = String.Empty;

			[BsonElement("d")]
			public byte[] Data { get; set; } = null!;

			[BsonElement("f")]
			public bool Finalized { get; set; }

			[BsonElement("t")]
			public DateTime LastAccessTime { get; set; } = DateTime.UtcNow;

			public NamespaceId NamespaceId => new NamespaceId(Id.Split('/')[0]);
			public BucketId BucketId => new BucketId(Id.Split('/')[1]);
			public RefId RefId => new RefId(IoHash.Parse(Id.Split('/')[2]));

			public CbObject Value => new CbObject(Data);
		}

		IMongoCollection<RefImpl> Refs;
		IStorageBackend StorageBackend;

		/// <summary>
		/// 
		/// </summary>
		/// <param name="DatabaseService"></param>
		/// <param name="StorageBackend"></param>
		public BasicStorageClient(DatabaseService DatabaseService, IStorageBackend<BasicStorageClient> StorageBackend)
		{
			Refs = DatabaseService.GetCollection<RefImpl>("Storage.RefsV2");
			this.StorageBackend = StorageBackend;
		}

		/// <inheritdoc/>
		public async Task<Stream> ReadBlobAsync(NamespaceId NamespaceId, IoHash Hash, CancellationToken CancellationToken)
		{
			string Path = GetFullBlobPath(NamespaceId, Hash);

			Stream? Stream = await StorageBackend.ReadAsync(Path);
			if(Stream == null)
			{
				throw new BlobNotFoundException(NamespaceId, Hash);
			}

			return Stream;
		}

		/// <inheritdoc/>
		public async Task WriteBlobAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream, CancellationToken CancellationToken)
		{
			byte[] Data;
			using (MemoryStream Buffer = new MemoryStream())
			{
				await Stream.CopyToAsync(Buffer, CancellationToken);
				Data = Buffer.ToArray();
			}

			IoHash DataHash = IoHash.Compute(Data);
			if (Hash != DataHash)
			{
				throw new InvalidDataException($"Incorrect hash for data (was {DataHash}, expected {Hash})");
			}

			string Path = GetFullBlobPath(NamespaceId, Hash);
			await StorageBackend.WriteBytesAsync(Path, Data);
		}

		/// <inheritdoc/>
		public async Task<IRef> GetRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken)
		{
			string Id = GetFullRefId(NamespaceId, BucketId, RefId);

			RefImpl? RefImpl = await Refs.FindOneAndUpdateAsync<RefImpl>(x => x.Id == Id, Builders<RefImpl>.Update.Set(x => x.LastAccessTime, DateTime.UtcNow), new FindOneAndUpdateOptions<RefImpl> { ReturnDocument = ReturnDocument.After }, CancellationToken);
			if (RefImpl == null || !RefImpl.Finalized)
			{
				throw new RefNotFoundException(NamespaceId, BucketId, RefId);
			}

			return RefImpl;
		}

		/// <inheritdoc/>
		public async Task<bool> HasRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken)
		{
			string Id = GetFullRefId(NamespaceId, BucketId, RefId);
			RefImpl? RefImpl = await Refs.Find<RefImpl>(x => x.Id == Id).FirstOrDefaultAsync(CancellationToken);
			return RefImpl != null;
		}

		/// <inheritdoc/>
		public async Task<List<IoHash>> TrySetRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CbObject Value, CancellationToken CancellationToken)
		{
			RefImpl NewRef = new RefImpl();
			NewRef.Id = GetFullRefId(NamespaceId, BucketId, RefId);
			NewRef.Data = Value.GetView().ToArray();
			await Refs.ReplaceOneAsync(x => x.Id == NewRef.Id, NewRef, new ReplaceOptions { IsUpsert = true }, CancellationToken);

			IoHash Hash = IoHash.Compute(Value.GetView().Span);
			return await TryFinalizeRefAsync(NamespaceId, BucketId, RefId, Hash, CancellationToken);
		}

		/// <inheritdoc/>
		public async Task<List<IoHash>> TryFinalizeRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, IoHash Hash, CancellationToken CancellationToken)
		{
			string Id = GetFullRefId(NamespaceId, BucketId, RefId);

			RefImpl Ref = await Refs.Find(x => x.Id == Id).FirstAsync(CancellationToken);

			HashSet<IoHash> MissingHashes = new HashSet<IoHash>();
			if (!Ref.Finalized)
			{
				HashSet<IoHash> CheckedObjectHashes = new HashSet<IoHash>();
				HashSet<IoHash> CheckedBinaryHashes = new HashSet<IoHash>();
				await FinalizeInternalAsync(Ref.NamespaceId, Ref.Value, CheckedObjectHashes, CheckedBinaryHashes, MissingHashes);

				if (MissingHashes.Count == 0)
				{
					await Refs.UpdateOneAsync(x => x.Id == Id, Builders<RefImpl>.Update.Set(x => x.Finalized, true), cancellationToken: CancellationToken);
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
				string ObjPath = GetFullBlobPath(NamespaceId, NewHash);

				ReadOnlyMemory<byte>? Data = await StorageBackend.ReadBytesAsync(ObjPath);
				if (Data == null)
				{
					MissingHashes.Add(NewHash);
				}
				else
				{
					await FinalizeInternalAsync(NamespaceId, new CbObject(Data.Value), CheckedObjectHashes, CheckedBinaryHashes, MissingHashes);
				}
			}

			foreach (IoHash NewBinaryHash in NewBinaryHashes)
			{
				string Path = GetFullBlobPath(NamespaceId, NewBinaryHash);
				if (!await StorageBackend.ExistsAsync(Path))
				{
					MissingHashes.Add(NewBinaryHash);
				}
			}
		}

		/// <inheritdoc/>
		public async Task<bool> DeleteRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken)
		{
			string Id = GetFullRefId(NamespaceId, BucketId, RefId);
			DeleteResult Result = await Refs.DeleteOneAsync(x => x.Id == Id, CancellationToken);
			return Result.DeletedCount > 0;
		}

		/// <inheritdoc/>
		public async Task<List<RefId>> FindMissingRefsAsync(NamespaceId NamespaceId, BucketId BucketId, List<RefId> RefIds, CancellationToken CancellationToken)
		{
			List<RefId> MissingRefIds = new List<RefId>();
			foreach (RefId RefId in RefIds)
			{
				if(!await HasRefAsync(NamespaceId, BucketId, RefId, CancellationToken))
				{
					MissingRefIds.Add(RefId);
				}
			}
			return MissingRefIds;
		}

		/// <summary>
		/// Gets the formatted id for a particular ref
		/// </summary>
		static string GetFullRefId(NamespaceId NamespaceId, BucketId BucketId, RefId RefId)
		{
			return $"{NamespaceId}/{BucketId}/{RefId}";
		}

		/// <summary>
		/// Gets the path for a blob
		/// </summary>
		static string GetFullBlobPath(NamespaceId NamespaceId, IoHash Hash)
		{
			string HashText = Hash.ToString();
			return $"blobs2/{NamespaceId}/{HashText[0..2]}/{HashText[2..4]}/{HashText}.blob";
		}
	}
}
