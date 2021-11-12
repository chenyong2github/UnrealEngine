// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using HordeServer.Services;
using HordeServer.Storage;
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
	using NamespaceId = StringId<INamespace>;
	using BucketId = StringId<IBucket>;

	[TestClass]
	public class StorageTests : TestSetup
	{
		[TestMethod]
		public async Task BlobCollectionTest()
		{
			NamespaceId NamespaceId = new NamespaceId("test-ns");

			IBlobCollection BlobCollection = ServiceProvider.GetRequiredService<IBlobCollection>();

			byte[] TestData = Encoding.UTF8.GetBytes("Hello world");
			IoHash Hash = IoHash.Compute(TestData);

			Assert.IsNull(await BlobCollection.TryReadStreamAsync(NamespaceId, Hash));
			Assert.IsFalse(await BlobCollection.ExistsAsync(NamespaceId, Hash));

			IoHash ReturnedHash = await BlobCollection.WriteBytesAsync(NamespaceId, TestData);
			Assert.AreEqual(Hash, ReturnedHash);
			Assert.IsTrue(await BlobCollection.ExistsAsync(NamespaceId, Hash));

			ReadOnlyMemory<byte>? StoredData = await BlobCollection.TryReadBytesAsync(NamespaceId, Hash);
			Assert.IsNotNull(StoredData);
			Assert.IsTrue(TestData.AsSpan().SequenceEqual(StoredData.Value.Span));
			Assert.IsTrue(await BlobCollection.ExistsAsync(NamespaceId, Hash));

			NamespaceId OtherNamespaceId = new NamespaceId("other-ns");
			Assert.IsNull(await BlobCollection.TryReadStreamAsync(OtherNamespaceId, Hash));
			Assert.IsFalse(await BlobCollection.ExistsAsync(OtherNamespaceId, Hash));
		}

		[TestMethod]
		public async Task ObjectCollectionTest()
		{
			NamespaceId NamespaceId = new NamespaceId("test-ns");

			IObjectCollection ObjectCollection = ServiceProvider.GetRequiredService<IObjectCollection>();

			CbObject ObjectD = CbObject.Build(Writer =>
			{
				Writer.WriteString("hello", "field");
			});
			IoHash HashD = ObjectD.GetHash();

			Assert.IsFalse(await ObjectCollection.ExistsAsync(NamespaceId, HashD));
			Assert.IsNull(await ObjectCollection.GetAsync(NamespaceId, HashD));

			await ObjectCollection.AddAsync(NamespaceId, HashD, ObjectD);

			Assert.IsTrue(await ObjectCollection.ExistsAsync(NamespaceId, HashD));

			CbObject? ReturnedObjectD = await ObjectCollection.GetAsync(NamespaceId, HashD);
			Assert.IsNotNull(ReturnedObjectD);
			Assert.IsTrue(ReturnedObjectD!.GetView().Span.SequenceEqual(ObjectD.GetView().Span));
		}

		[TestMethod]
		public async Task RefCollectionTest()
		{
			IBlobCollection BlobCollection = ServiceProvider.GetRequiredService<IBlobCollection>();
			IObjectCollection ObjectCollection = ServiceProvider.GetRequiredService<IObjectCollection>();
			IRefCollection RefCollection = ServiceProvider.GetRequiredService<IRefCollection>();

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
			const string RefName = "refname";

			// Check that setting the ref to E fails with the correct missing hashes
			List<IoHash> MissingHashes = await RefCollection.SetAsync(NamespaceId, BucketId, RefName, ObjectE);
			Assert.AreEqual(2, MissingHashes.Count);
			Assert.IsTrue(MissingHashes.Contains(HashA));
			Assert.IsTrue(MissingHashes.Contains(HashD));

			IRef? Ref = await RefCollection.GetAsync(NamespaceId, BucketId, RefName);
			Assert.IsNotNull(Ref);
			Assert.IsFalse(Ref!.Finalized);
			Assert.IsTrue(Ref!.Value.GetView().Span.SequenceEqual(ObjectE.GetView().Span));

			// Add object D, and check that changes the missing hashes to just the blobs
			await ObjectCollection.AddAsync(NamespaceId, HashD, ObjectD);

			MissingHashes = await RefCollection.FinalizeAsync(Ref);
			Assert.AreEqual(3, MissingHashes.Count);
			Assert.IsTrue(MissingHashes.Contains(HashA));
			Assert.IsTrue(MissingHashes.Contains(HashB));
			Assert.IsTrue(MissingHashes.Contains(HashC));

			Ref = await RefCollection.GetAsync(NamespaceId, BucketId, RefName);
			Assert.IsNotNull(Ref);
			Assert.IsFalse(Ref!.Finalized);

			// Add blobs A, B and C and check that the object can be finalized
			await BlobCollection.WriteBytesAsync(NamespaceId, HashA, BlobA);
			await BlobCollection.WriteBytesAsync(NamespaceId, HashB, BlobB);
			await BlobCollection.WriteBytesAsync(NamespaceId, HashC, BlobC);

			MissingHashes = await RefCollection.FinalizeAsync(Ref);
			Assert.AreEqual(0, MissingHashes.Count);

			Ref = await RefCollection.GetAsync(NamespaceId, BucketId, RefName);
			Assert.IsNotNull(Ref);
			Assert.IsTrue(Ref!.Finalized);

			// Add a new ref to existing objects and check it finalizes correctly 
			const string RefName2 = "refname";

			MissingHashes = await RefCollection.SetAsync(NamespaceId, BucketId, RefName2, ObjectE);
			Assert.AreEqual(0, MissingHashes.Count);

			Ref = await RefCollection.GetAsync(NamespaceId, BucketId, RefName2);
			Assert.IsNotNull(Ref);
			Assert.IsTrue(Ref!.Finalized);
		}
	}
}
