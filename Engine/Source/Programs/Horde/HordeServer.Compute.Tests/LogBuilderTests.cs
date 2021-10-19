// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Logs;
using HordeServer.Logs.Builder;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace HordeServerTests
{
	using LogId = ObjectId<ILogFile>;

	[TestClass]
	public class LogBuilderTests : DatabaseIntegrationTest
	{
		[TestMethod]
		public async Task TestLocalLogBuilder()
		{
			ILogBuilder Builder = new LocalLogBuilder();
			await TestBuilder(Builder);
		}

		[TestMethod]
		public async Task TestRedisLogBuilder()
		{
			ILogBuilder Builder = new RedisLogBuilder(GetRedisConnectionPool(), NullLogger.Instance);
			await TestBuilder(Builder);
		}

		public async Task TestBuilder(ILogBuilder Builder)
		{
			LogId LogId = LogId.GenerateNewId();

			const long Offset = 100;
			Assert.IsTrue(await Builder.AppendAsync(LogId, Offset, Offset, 0, 1, Encoding.UTF8.GetBytes("hello\n"), LogType.Text));

			LogChunkData? Chunk1 = await Builder.GetChunkAsync(LogId, Offset, 0);
			Assert.IsNotNull(Chunk1);
			Assert.AreEqual(1, Chunk1!.LineCount);
			Assert.AreEqual(6, Chunk1!.Length);
			Assert.AreEqual(1, Chunk1!.SubChunks.Count);
			Assert.AreEqual(1, Chunk1!.SubChunks[0].LineCount);
			Assert.AreEqual(6, Chunk1!.SubChunks[0].Length);

			Assert.IsTrue(await Builder.AppendAsync(LogId, Offset, Offset + 6, 1, 1, Encoding.UTF8.GetBytes("world\n"), LogType.Text));

			LogChunkData? Chunk2 = await Builder.GetChunkAsync(LogId, Offset, 1);
			Assert.IsNotNull(Chunk2);
			Assert.AreEqual(2, Chunk2!.LineCount);
			Assert.AreEqual(12, Chunk2!.Length);
			Assert.AreEqual(1, Chunk2!.SubChunks.Count);
			Assert.AreEqual(2, Chunk2!.SubChunks[0].LineCount);
			Assert.AreEqual(12, Chunk2!.SubChunks[0].Length);

			await Builder.CompleteSubChunkAsync(LogId, Offset);
			Assert.IsTrue(await Builder.AppendAsync(LogId, Offset, Offset + 12, 2, 1, Encoding.UTF8.GetBytes("foo\n"), LogType.Text));
			Assert.IsTrue(await Builder.AppendAsync(LogId, Offset, Offset + 16, 3, 2, Encoding.UTF8.GetBytes("bar\nbaz\n"), LogType.Text));
			await Builder.CompleteSubChunkAsync(LogId, Offset);

			LogChunkData? Chunk3 = await Builder.GetChunkAsync(LogId, Offset, 4);
			Assert.IsNotNull(Chunk3);
			Assert.AreEqual(5, Chunk3!.LineCount);
			Assert.AreEqual(24, Chunk3!.Length);
			Assert.AreEqual(2, Chunk3!.SubChunks.Count);
			Assert.AreEqual(2, Chunk3!.SubChunks[0].LineCount);
			Assert.AreEqual(12, Chunk3!.SubChunks[0].Length);
			Assert.AreEqual(3, Chunk3!.SubChunks[1].LineCount);
			Assert.AreEqual(12, Chunk3!.SubChunks[1].Length);

			Assert.AreEqual(0, Chunk3!.GetLineOffsetWithinChunk(0));
			Assert.AreEqual(6, Chunk3!.GetLineOffsetWithinChunk(1));
			Assert.AreEqual(12, Chunk3!.GetLineOffsetWithinChunk(2));
			Assert.AreEqual(16, Chunk3!.GetLineOffsetWithinChunk(3));
			Assert.AreEqual(20, Chunk3!.GetLineOffsetWithinChunk(4));

			List<(LogId, long)> Chunks1 = await Builder.TouchChunksAsync(TimeSpan.Zero);
			Assert.AreEqual(1, Chunks1.Count);
			Assert.AreEqual((LogId, Offset), Chunks1[0]);

			await Builder.CompleteChunkAsync(LogId, Offset);

			LogChunkData? Chunk4 = await Builder.GetChunkAsync(LogId, Offset, 5);
			Assert.AreEqual(24, Chunk4!.Length);

			List<(LogId, long)> Chunks2 = await Builder.TouchChunksAsync(TimeSpan.Zero);
			Assert.AreEqual(1, Chunks2.Count);

			await Builder.RemoveChunkAsync(LogId, Offset);

			List<(LogId, long)> Chunks3 = await Builder.TouchChunksAsync(TimeSpan.Zero);
			Assert.AreEqual(0, Chunks3.Count);
		}
	}
}
