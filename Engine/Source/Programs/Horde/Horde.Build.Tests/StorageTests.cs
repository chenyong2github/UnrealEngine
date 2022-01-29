// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace HordeServerTests
{
	[TestClass]
	public class StorageTests : TestSetup
	{
		[TestMethod]
		public async Task BlobCollectionTest()
		{
			NamespaceId NamespaceId = new NamespaceId("test-ns");

			IStorageClient StorageClient = ServiceProvider.GetRequiredService<IStorageClient>();

			byte[] TestData = Encoding.UTF8.GetBytes("Hello world");
			IoHash Hash = IoHash.Compute(TestData);

			await Assert.ThrowsExceptionAsync<BlobNotFoundException>(() => StorageClient.ReadBlobAsync(NamespaceId, Hash));

			IoHash ReturnedHash = await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, TestData);
			Assert.AreEqual(Hash, ReturnedHash);
			Assert.IsNotNull(await StorageClient.ReadBlobToMemoryAsync(NamespaceId, Hash));

			ReadOnlyMemory<byte> StoredData = await StorageClient.ReadBlobToMemoryAsync(NamespaceId, Hash);
			Assert.IsTrue(TestData.AsSpan().SequenceEqual(StoredData.Span));

			NamespaceId OtherNamespaceId = new NamespaceId("other-ns");
			await Assert.ThrowsExceptionAsync<BlobNotFoundException>(() => StorageClient.ReadBlobAsync(OtherNamespaceId, Hash));
		}

		[TestMethod]
		public async Task ObjectCollectionTest()
		{
			NamespaceId NamespaceId = new NamespaceId("test-ns");

			IStorageClient StorageClient = ServiceProvider.GetRequiredService<IStorageClient>();

			CbObject ObjectD = CbObject.Build(Writer =>
			{
				Writer.WriteString("hello", "field");
			});
			IoHash HashD = ObjectD.GetHash();

			await Assert.ThrowsExceptionAsync<BlobNotFoundException>(() => StorageClient.ReadBlobToMemoryAsync(NamespaceId, HashD));

			await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, HashD, ObjectD.GetView());

			CbObject ReturnedObjectD = new CbObject(await StorageClient.ReadBlobToMemoryAsync(NamespaceId, HashD));
			Assert.IsTrue(ReturnedObjectD!.GetView().Span.SequenceEqual(ObjectD.GetView().Span));
		}

		[TestMethod]
		public async Task RefCollectionTest()
		{
			IStorageClient StorageClient = ServiceProvider.GetRequiredService<IStorageClient>();

			byte[] BlobA = Encoding.UTF8.GetBytes("This is blob A");
			IoHash HashA = IoHash.Compute(BlobA);

			byte[] BlobB = Encoding.UTF8.GetBytes("This is blob B");
			IoHash HashB = IoHash.Compute(BlobB);

			byte[] BlobC = Encoding.UTF8.GetBytes("This is blob C");
			IoHash HashC = IoHash.Compute(BlobC);

			CbObject ObjectD = CbObject.Build(Writer =>
			{
				Writer.WriteBinaryAttachment("A", HashA);
				Writer.WriteBinaryAttachment("B", HashB);
				Writer.WriteBinaryAttachment("C", HashC);
			});
			IoHash HashD = ObjectD.GetHash();

			CbObject ObjectE = CbObject.Build(Writer =>
			{
				Writer.WriteBinaryAttachment("A", HashA);
				Writer.WriteObjectAttachment("D", HashD);
			});
			IoHash HashE = ObjectE.GetHash();

			NamespaceId NamespaceId = new NamespaceId("test-ns");
			BucketId BucketId = new BucketId("test-bkt");
			RefId RefId = new RefId("refname");

			// Check that setting the ref to E fails with the correct missing hashes
			List<IoHash> MissingHashes = await StorageClient.TrySetRefAsync(NamespaceId, BucketId, RefId, ObjectE);
			Assert.AreEqual(2, MissingHashes.Count);
			Assert.IsTrue(MissingHashes.Contains(HashA));
			Assert.IsTrue(MissingHashes.Contains(HashD));

			await Assert.ThrowsExceptionAsync<RefNotFoundException>(() => StorageClient.GetRefAsync(NamespaceId, BucketId, RefId));

			// Add object D, and check that changes the missing hashes to just the blobs
			await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, HashD, ObjectD.GetView());

			MissingHashes = await StorageClient.TryFinalizeRefAsync(NamespaceId, BucketId, RefId, HashE);
			Assert.AreEqual(3, MissingHashes.Count);
			Assert.IsTrue(MissingHashes.Contains(HashA));
			Assert.IsTrue(MissingHashes.Contains(HashB));
			Assert.IsTrue(MissingHashes.Contains(HashC));

			await Assert.ThrowsExceptionAsync<RefNotFoundException>(() => StorageClient.GetRefAsync(NamespaceId, BucketId, RefId));

			// Add blobs A, B and C and check that the object can be finalized
			await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, HashA, BlobA);
			await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, HashB, BlobB);
			await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, HashC, BlobC);

			MissingHashes = await StorageClient.TryFinalizeRefAsync(NamespaceId, BucketId, RefId, HashE);
			Assert.AreEqual(0, MissingHashes.Count);

			IRef Ref = await StorageClient.GetRefAsync(NamespaceId, BucketId, RefId);
			Assert.IsNotNull(Ref);

			// Add a new ref to existing objects and check it finalizes correctly 
			RefId RefId2 = new RefId("refname");

			MissingHashes = await StorageClient.TrySetRefAsync(NamespaceId, BucketId, RefId2, ObjectE);
			Assert.AreEqual(0, MissingHashes.Count);

			Ref = await StorageClient.GetRefAsync(NamespaceId, BucketId, RefId2);
			Assert.IsNotNull(Ref);
		}
	}
}
