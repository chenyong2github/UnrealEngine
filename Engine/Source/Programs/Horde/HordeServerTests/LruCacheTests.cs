// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace HordeServerTests
{
	[TestClass]
	public class LruCacheTests : IDisposable
	{
		FileReference IndexFile;
		FileReference DataFile;
		LruCache Cache;

		public LruCacheTests()
		{
			this.IndexFile = new FileReference("test.idx");
			this.DataFile = new FileReference("test.dat");

			Cache = LruCache.CreateNew(IndexFile, DataFile, 2048, 1024 * 1024);
		}

		public void Dispose()
		{
			Cache.Dispose();
		}

		[TestMethod]
		public void EmptyObject()
		{
			IoHash Hash = Cache.Add(Array.Empty<byte>());
			Assert.AreEqual(1, Cache.NumItems);

			using (LruCache.View View = Cache.LockView())
			{
				byte[] Data = View.Get(Hash).ToArray();
				Assert.AreEqual(0, Data.Length);
			}
		}

		[TestMethod]
		public void DuplicateObjects()
		{
			Cache.Add(new byte[] { 1, 2, 3 });
			Cache.Add(new byte[] { 1, 2, 3 });
			Cache.Add(new byte[] { 1, 2, 4 });
			Assert.AreEqual(2, Cache.NumItems);
			Assert.AreEqual(6, Cache.NumBytes);
			Assert.AreEqual(128, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, Cache.NumBytesWithPageSlack);
		}

		static byte[] MakeBlockWithIndex(int Size, int Index)
		{
			byte[] Data = new byte[Size];
			BinaryPrimitives.WriteInt32LittleEndian(Data, Index);
			return Data;
		}

		[TestMethod]
		public void LargeObjectPacking()
		{
			Cache.Add(MakeBlockWithIndex(1000, 1));
			Assert.AreEqual(1, Cache.NumItems);
			Assert.AreEqual(1000, Cache.NumBytes);
			Assert.AreEqual(1024, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, Cache.NumBytesWithPageSlack);

			Cache.Add(MakeBlockWithIndex(1000, 2));
			Assert.AreEqual(2, Cache.NumItems);
			Assert.AreEqual(2000, Cache.NumBytes);
			Assert.AreEqual(2048, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, Cache.NumBytesWithPageSlack);

			Cache.Add(MakeBlockWithIndex(1000, 3));
			Assert.AreEqual(3, Cache.NumItems);
			Assert.AreEqual(3000, Cache.NumBytes);
			Assert.AreEqual(3072, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, Cache.NumBytesWithPageSlack);

			Cache.Add(MakeBlockWithIndex(1000, 4));
			Assert.AreEqual(4, Cache.NumItems);
			Assert.AreEqual(4000, Cache.NumBytes);
			Assert.AreEqual(4096, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, Cache.NumBytesWithPageSlack);

			Cache.Add(MakeBlockWithIndex(1000, 5));
			Assert.AreEqual(5, Cache.NumItems);
			Assert.AreEqual(5000, Cache.NumBytes);
			Assert.AreEqual(5120, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(8192, Cache.NumBytesWithPageSlack);
		}

		[TestMethod]
		public void SmallObjectPacking()
		{
			Cache.Add(MakeBlockWithIndex(64, 1));
			Assert.AreEqual(1, Cache.NumItems);
			Assert.AreEqual(64, Cache.NumBytes);
			Assert.AreEqual(64, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, Cache.NumBytesWithPageSlack);

			Cache.Add(MakeBlockWithIndex(64, 2));
			Assert.AreEqual(2, Cache.NumItems);
			Assert.AreEqual(128, Cache.NumBytes);
			Assert.AreEqual(128, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, Cache.NumBytesWithPageSlack);

			Cache.Add(MakeBlockWithIndex(64, 3));
			Assert.AreEqual(3, Cache.NumItems);
			Assert.AreEqual(192, Cache.NumBytes);
			Assert.AreEqual(192, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096, Cache.NumBytesWithPageSlack);
		}

		[TestMethod]
		public void FillItems()
		{
			for (int Idx = 0; Idx < 2048; Idx++)
			{
				byte[] Data = MakeBlockWithIndex(64, Idx);
				Cache.Add(Data);
			}
			Assert.AreEqual(2048, Cache.NumItems);

			Cache.Add(new byte[] { 1, 2, 3 });
			Assert.AreEqual(2048, Cache.NumItems);
		}

		[TestMethod]
		public void FillCapacity()
		{
			for (int Idx = 0; Idx < 1024; Idx++)
			{
				byte[] Data = MakeBlockWithIndex(1024, Idx);
				Cache.Add(Data);
			}
			Assert.AreEqual(1024, Cache.NumItems);

			Cache.Add(new byte[] { 1, 2, 3 });
			Assert.AreEqual(1024, Cache.NumItems);
		}

		[TestMethod]
		public void HugeObjects()
		{
			byte[] Data = new byte[4096 + 4096 + 2000];

			Random Random = new Random(0);
			Random.NextBytes(Data);

			IoHash Hash = Cache.Add(Data);
			Assert.AreEqual(1, Cache.NumItems);
			Assert.AreEqual(Data.Length, Cache.NumBytes);
			Assert.AreEqual(4096 + 4096 + 2048, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(4096 + 4096 + 4096, Cache.NumBytesWithPageSlack);

			using LruCache.View View = Cache.LockView();
			ReadOnlyMemory<byte> LookupData = View.Get(Hash);
			Assert.IsTrue(Data.AsSpan().SequenceEqual(LookupData.Span));
		}

		[TestMethod]
		public void HugeObjects2()
		{
			const int NumItems = 5;
			for (int Idx = 0; Idx < NumItems; Idx++)
			{
				byte[] Data = MakeBlockWithIndex(2000 + (Idx * 2048), Idx);
				Cache.Add(Data);
			}

			Assert.AreEqual(NumItems, Cache.NumItems);

			using (LruCache.View View = Cache.LockView())
			{
				for (int Idx = 0; Idx < NumItems; Idx++)
				{
					byte[] Data = MakeBlockWithIndex(2000 + (Idx * 2048), Idx);
					IoHash Hash = IoHash.Compute(Data);
					ReadOnlyMemory<byte> OtherData = View.Get(Hash);
					Assert.IsTrue(Data.AsSpan().SequenceEqual(OtherData.Span));
				}
			}
		}

		[TestMethod]
		public async Task SmallObjectSerialization()
		{
			for (int Idx = 0; Idx < 1000; Idx++)
			{
				byte[] Data = MakeBlockWithIndex(4 + Idx, Idx);
				Cache.Add(Data);
			}

			CheckSmallObjects();

			Assert.AreEqual(1000, Cache.NumItems);
			long NumBytes = Cache.NumBytes;
			long NumBytesWithBlockSlack = Cache.NumBytesWithBlockSlack;
			long NumBytesWithPageSlack = Cache.NumBytesWithPageSlack;

			await Cache.SaveAsync();
			Cache.Dispose();

			Cache = await LruCache.OpenAsync(IndexFile, DataFile);

			Assert.AreEqual(1000, Cache.NumItems);
			Assert.AreEqual(NumBytes, Cache.NumBytes);
			Assert.AreEqual(NumBytesWithBlockSlack, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(NumBytesWithPageSlack, Cache.NumBytesWithPageSlack);

			CheckSmallObjects();
		}

		void CheckSmallObjects()
		{
			using (LruCache.View View = Cache.LockView())
			{
				for (int Idx = 0; Idx < 1000; Idx++)
				{
					byte[] Data = MakeBlockWithIndex(4 + Idx, Idx);
					IoHash Hash = IoHash.Compute(Data);
					ReadOnlyMemory<byte> OtherData = View.Get(Hash);
					Assert.IsTrue(Data.AsSpan().SequenceEqual(OtherData.Span));
				}
			}
		}

		[TestMethod]
		public async Task LargeObjectSerialization()
		{
			const int NumItems = 10;
			for (int Idx = 0; Idx < NumItems; Idx++)
			{
				byte[] Data = MakeBlockWithIndex(2000 + (Idx * 2048), Idx);
				Cache.Add(Data);
			}

			CheckLargeObjects(NumItems);

			Assert.AreEqual(NumItems, Cache.NumItems);
			long NumBytes = Cache.NumBytes;
			long NumBytesWithBlockSlack = Cache.NumBytesWithBlockSlack;
			long NumBytesWithPageSlack = Cache.NumBytesWithPageSlack;

			await Cache.SaveAsync();
			Cache.Dispose();

			Cache = await LruCache.OpenAsync(IndexFile, DataFile);

			Assert.AreEqual(NumItems, Cache.NumItems);
			Assert.AreEqual(NumBytes, Cache.NumBytes);
			Assert.AreEqual(NumBytesWithBlockSlack, Cache.NumBytesWithBlockSlack);
			Assert.AreEqual(NumBytesWithPageSlack, Cache.NumBytesWithPageSlack);

			CheckLargeObjects(NumItems);
		}

		void CheckLargeObjects(int NumItems)
		{
			using (LruCache.View View = Cache.LockView())
			{
				for (int Idx = 0; Idx < NumItems; Idx++)
				{
					byte[] Data = MakeBlockWithIndex(2000 + (Idx * 2048), Idx);
					IoHash Hash = IoHash.Compute(Data);
					ReadOnlyMemory<byte> OtherData = View.Get(Hash);
					Assert.IsTrue(Data.AsSpan().SequenceEqual(OtherData.Span));
				}
			}
		}

		[TestMethod]
		public async Task ExpireItems()
		{
			for (int Idx = 0; Idx < 1000; Idx++)
			{
				byte[] Data = MakeBlockWithIndex(4 + Idx, Idx);
				Cache.Add(Data);
			}

			long TrimSize = Cache.NumBytesWithBlockSlack;
			Cache.NextGeneration();

			List<IoHash> TestHashes = new List<IoHash>();
			for (int Idx = 1000; Idx < 2000; Idx++)
			{
				byte[] Data = MakeBlockWithIndex(4 + Idx, Idx);
				TestHashes.Add(Cache.Add(Data));
			}

			long DesiredSize = Cache.NumBytesWithBlockSlack - TrimSize;
			await Cache.TrimAsync(DesiredSize);

			Assert.AreEqual(DesiredSize, Cache.NumBytesWithBlockSlack);

			using(LruCache.View View = Cache.LockView())
			{
				for (int Idx = 0; Idx < 100; Idx++)
				{
					ReadOnlyMemory<byte> Memory = View.Get(TestHashes[Idx]);
					Assert.IsFalse(Memory.IsEmpty);
				}
			}
		}
	}
}
