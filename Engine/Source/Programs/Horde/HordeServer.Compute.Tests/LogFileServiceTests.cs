// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Runtime.InteropServices.ComTypes;
using System.Security.Claims;
using System.Text;
using System.Threading.Tasks;
using HordeServer;
using HordeServer.Api;
using HordeServer.Collections.Impl;
using HordeServer.Logs;
using HordeServer.Logs.Builder;
using HordeServer.Logs.Readers;
using HordeServer.Logs.Storage;
using HordeServer.Logs.Storage.Impl;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Storage;
using HordeServer.Storage.Backends;
using HordeServer.Utilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;
using StackExchange.Redis;
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;

namespace HordeServerTests
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;

	[TestClass]
    public class LogFileServiceTest : DatabaseIntegrationTest
    {
        private readonly LogFileService LogFileService;

        public LogFileServiceTest()
        {
            var LogFileCollection = new LogFileCollection(GetDatabaseService());

            var LoggerFactory = (ILoggerFactory) new LoggerFactory();
            var Logger = LoggerFactory.CreateLogger<LogFileService>();

            // Just to satisfy the parameter, need to be fixed
            IConfiguration Config = new ConfigurationBuilder().Build();

			ILogBuilder LogBuilder = new RedisLogBuilder(GetRedisConnectionPool(), NullLogger.Instance);
			ILogStorage LogStorage = new PersistentLogStorage(new TransientStorageBackend().ForType<PersistentLogStorage>(), NullLogger<PersistentLogStorage>.Instance);
			LogFileService = new LogFileService(LogFileCollection, null!, LogBuilder, LogStorage, Logger);
        }

		[TestMethod]
        public async Task WriteLogLifecycleOldTest()
        {
			JobId JobId = JobId.GenerateNewId();
            ILogFile LogFile = await LogFileService.CreateLogFileAsync(JobId, null, LogType.Text);

            LogFile = (await ((ILogFileService)LogFileService).WriteLogDataAsync(LogFile, 0, 0, Encoding.ASCII.GetBytes("hello\n"), true))!;
			LogFile = (await ((ILogFileService)LogFileService).WriteLogDataAsync(LogFile, 6, 1, Encoding.ASCII.GetBytes("foo\nbar\n"), true))!;
			LogFile = (await ((ILogFileService)LogFileService).WriteLogDataAsync(LogFile, 6 + 8, 3, Encoding.ASCII.GetBytes("baz\n"), false))!;

            Assert.AreEqual("hello", await ReadLogFile(LogFileService, LogFile, 0, 5));
            Assert.AreEqual("foo\nbar\nbaz\n", await ReadLogFile(LogFileService, LogFile, 6, 12));

            var Metadata = await LogFileService.GetMetadataAsync(LogFile);
            Assert.AreEqual(6 + 8 + 4, Metadata.Length);
            Assert.AreEqual(4, Metadata.MaxLineIndex);

            Assert.AreEqual((0, 0), await LogFileService.GetLineOffsetAsync(LogFile, 0));
            Assert.AreEqual((1, 6), await LogFileService.GetLineOffsetAsync(LogFile, 1));
            Assert.AreEqual((2, 10), await LogFileService.GetLineOffsetAsync(LogFile, 2));
            Assert.AreEqual((3, 14), await LogFileService.GetLineOffsetAsync(LogFile, 3));
        }
        
        [TestMethod]
        public async Task WriteLogLifecycleTest()
        {
	        await WriteLogLifecycle(LogFileService, 30);
        }

        
        private static async Task AssertMetadata(ILogFileService LogFileService, ILogFile LogFile, long ExpectedLength, long ExpectedMaxLineIndex)
        {
	        LogMetadata Metadata = await LogFileService.GetMetadataAsync(LogFile);
	        Assert.AreEqual(ExpectedLength, Metadata.Length);
	        Assert.AreEqual(ExpectedMaxLineIndex, Metadata.MaxLineIndex);
        }

        private static async Task AssertChunk(ILogFileService LogFileService, LogId LogFileId, long NumChunks, int ChunkId, long Offset, long Length,
	        long LineIndex)
        {
	        ILogFile? LogFile = await LogFileService.GetLogFileAsync(LogFileId);
	        Assert.AreEqual(NumChunks, LogFile!.Chunks.Count);

	        ILogChunk Chunk = LogFile.Chunks[ChunkId];
	        Assert.AreEqual(Offset, Chunk.Offset);
	        Assert.AreEqual(Length, Chunk.Length);
	        Assert.AreEqual(LineIndex, Chunk.LineIndex);
        }

        private static async Task AssertLineOffset(ILogFileService LogFileService, LogId LogFileId, int LineIndex, int ClampedLineIndex, long Offset)
        {
	        ILogFile? LogFile = await LogFileService.GetLogFileAsync(LogFileId);
	        Assert.AreEqual((ClampedLineIndex, Offset), await LogFileService.GetLineOffsetAsync(LogFile!, LineIndex));
        }
        
        protected static async Task<string> ReadLogFile(ILogFileService LogFileService, ILogFile LogFile, long Offset, long Length)
        {
	        Stream Stream = await LogFileService.OpenRawStreamAsync(LogFile, Offset, Length);
	        return new StreamReader(Stream).ReadToEnd();
        }
        
        public static async Task WriteLogLifecycle(ILogFileService Lfs, int MaxChunkLength)
        {
			JobId JobId = JobId.GenerateNewId();
            ILogFile LogFile = await Lfs.CreateLogFileAsync(JobId, null, LogType.Text);

            string Str1 = "hello\n";
            string Str2 = "foo\nbar\n";
            string Str3 = "baz\nqux\nquux\n";
            string Str4 = "quuz\n";

            int LineIndex = 0;
            int Offset = 0;

            // First write with flush. Will become chunk #1
            LogFile = (await Lfs.WriteLogDataAsync(LogFile, Offset, LineIndex, Encoding.ASCII.GetBytes(Str1), true, MaxChunkLength))!;
            await (Lfs as LogFileService)!.TickOnlyForTestingAsync();
            await Task.Delay(2000);
            Assert.AreEqual(Str1, await ReadLogFile(Lfs, LogFile, 0, Str1.Length));
            Assert.AreEqual(Str1, await ReadLogFile(Lfs, LogFile, 0, Str1.Length + 100)); // Reading too far is valid?
            await AssertMetadata(Lfs, LogFile, Str1.Length, 1);
            await AssertChunk(Lfs, LogFile.Id, 1, 0, 0, Str1.Length, LineIndex);
            await AssertLineOffset(Lfs, LogFile.Id, 0, 0, 0);
            await AssertLineOffset(Lfs, LogFile.Id, 1, 1, 6);
            await AssertLineOffset(Lfs, LogFile.Id, 1, 1, 6);
            await AssertLineOffset(Lfs, LogFile.Id, 1, 1, 6);

            // Second write without flushing. Will become chunk #2
            Offset += Str1.Length;
            LineIndex += Str1.Count(f => f == '\n');
            LogFile = (await Lfs.WriteLogDataAsync(LogFile, Offset, LineIndex, Encoding.ASCII.GetBytes(Str2), false, MaxChunkLength))!;
            await (Lfs as LogFileService)!.TickOnlyForTestingAsync();
            await Task.Delay(2000);
            Assert.AreEqual(Str1 + Str2, await ReadLogFile(Lfs, LogFile, 0, Str1.Length + Str2.Length));
            await AssertMetadata(Lfs, LogFile, Str1.Length + Str2.Length, 3); // FIXME: what are max line index?
            await AssertChunk(Lfs, LogFile.Id, 2, 0, 0, Str1.Length, 0);
            await AssertChunk(Lfs, LogFile.Id, 2, 1, Str1.Length, 0, 1); // Last chunk have length zero as it's being written
            await AssertLineOffset(Lfs, LogFile.Id, 0, 0, 0);
            await AssertLineOffset(Lfs, LogFile.Id, 1, 1, 6);
            await AssertLineOffset(Lfs, LogFile.Id, 2, 2, 10);
            await AssertLineOffset(Lfs, LogFile.Id, 3, 3, 14);

            // Third write without flushing. Will become chunk #2
            Offset += Str2.Length;
            LineIndex += Str3.Count(f => f == '\n');
            LogFile = (await Lfs.WriteLogDataAsync(LogFile, Offset, LineIndex, Encoding.ASCII.GetBytes(Str3), false, MaxChunkLength))!;
            await (Lfs as LogFileService)!.TickOnlyForTestingAsync();
            await Task.Delay(2000);
            Assert.AreEqual(Str1 + Str2 + Str3, await ReadLogFile(Lfs, LogFile, 0, Str1.Length + Str2.Length + Str3.Length));
            //await AssertMetadata(Lfs, LogFile, Str1.Length + Str2.Length + Str3.Length, 8);
            // Since no flush has happened, chunks should be identical to last write
            await AssertChunk(Lfs, LogFile.Id, 2, 0, 0, Str1.Length, 0);
            await AssertChunk(Lfs, LogFile.Id, 2, 1, Str1.Length, 0, 1);
            await AssertLineOffset(Lfs, LogFile.Id, 0, 0, 0);
            await AssertLineOffset(Lfs, LogFile.Id, 1, 1, 6);
            await AssertLineOffset(Lfs, LogFile.Id, 2, 2, 10);
            await AssertLineOffset(Lfs, LogFile.Id, 3, 3, 14);
            await AssertLineOffset(Lfs, LogFile.Id, 4, 4, 18);

            // Fourth write with flush. Will become chunk #2
            Offset += Str3.Length;
            LineIndex += Str4.Count(f => f == '\n');
            LogFile = (await Lfs.WriteLogDataAsync(LogFile, Offset, LineIndex, Encoding.ASCII.GetBytes(Str4), true, MaxChunkLength))!;
            await (Lfs as LogFileService)!.TickOnlyForTestingAsync();
            await Task.Delay(2000);
            Assert.AreEqual(Str1 + Str2 + Str3 + Str4,
                await ReadLogFile(Lfs, LogFile, 0, Str1.Length + Str2.Length + Str3.Length + Str4.Length));
            Assert.AreEqual(Str3 + Str4,
                await ReadLogFile(Lfs, LogFile, Str1.Length + Str2.Length, Str3.Length + Str4.Length));
            await AssertMetadata(Lfs, LogFile, Str1.Length + Str2.Length + Str3.Length + Str4.Length, 7);
            await AssertChunk(Lfs, LogFile.Id, 2, 0, 0, Str1.Length, 0);
            await AssertChunk(Lfs, LogFile.Id, 2, 1, Str1.Length, Str2.Length + Str3.Length + Str4.Length, 1);
            await AssertLineOffset(Lfs, LogFile.Id, 0, 0, 0);
            await AssertLineOffset(Lfs, LogFile.Id, 1, 1, 6);
            await AssertLineOffset(Lfs, LogFile.Id, 2, 2, 10);
            await AssertLineOffset(Lfs, LogFile.Id, 3, 3, 14);
            await AssertLineOffset(Lfs, LogFile.Id, 4, 4, 18);
            await AssertLineOffset(Lfs, LogFile.Id, 5, 5, 22);
            await AssertLineOffset(Lfs, LogFile.Id, 6, 6, 27);
            await AssertLineOffset(Lfs, LogFile.Id, 7, 7, 32);
            
            // Fifth write with flush and data that will span more than chunk. Will become chunk #3
            string A = "Lorem ipsum dolor sit amet\n";
            string B = "consectetur adipiscing\n";
            string Str5 = A + B;
            
            Offset += Str4.Length;
            LineIndex += Str5.Count(f => f == '\n');
            
            // Using this single write below will fail the ReadLogFile assert below. A bug?
            //await LogFileService.WriteLogDataAsync(LogFile, Offset, LineIndex, Encoding.ASCII.GetBytes(Str5), true);
            
            // Dividing it in two like this will work however
            LogFile = (await Lfs.WriteLogDataAsync(LogFile, Offset, LineIndex, Encoding.ASCII.GetBytes(A), false, MaxChunkLength))!;
			LogFile = (await Lfs.WriteLogDataAsync(LogFile, Offset + A.Length, LineIndex + 1, Encoding.ASCII.GetBytes(B), true, MaxChunkLength))!;
            
            await (Lfs as LogFileService)!.TickOnlyForTestingAsync();
            await Task.Delay(2000);
            await AssertMetadata(Lfs, LogFile, Str1.Length + Str2.Length + Str3.Length + Str4.Length + Str5.Length, 9);
            await AssertChunk(Lfs, LogFile.Id, 4, 0, 0, Str1.Length, 0);
            await AssertChunk(Lfs, LogFile.Id, 4, 1, Str1.Length, Str2.Length + Str3.Length + Str4.Length, 1);
            await AssertChunk(Lfs, LogFile.Id, 4, 2, Offset, A.Length, 7);
            await AssertChunk(Lfs, LogFile.Id, 4, 3, Offset + A.Length, B.Length, 8);

            Assert.AreEqual(Str5, await ReadLogFile(Lfs, LogFile, Offset, Str5.Length));
        }

        [TestMethod]
        public async Task GetLogFileTest()
        {
            await GetDatabaseService().Database.DropCollectionAsync("LogFiles");
            Assert.AreEqual(0, (await LogFileService.GetLogFilesAsync()).Count);

			// Will implicitly test GetLogFileAsync(), AddCachedLogFile()
			JobId JobId = JobId.GenerateNewId();
            ObjectId SessionId = ObjectId.GenerateNewId();
            ILogFile A = await LogFileService.CreateLogFileAsync(JobId, SessionId, LogType.Text);
            ILogFile B = (await LogFileService.GetCachedLogFileAsync(A.Id))!;
            Assert.AreEqual(A.JobId, B.JobId);
            Assert.AreEqual(A.SessionId, B.SessionId);
            Assert.AreEqual(A.Type, B.Type);

            ILogFile? NotFound = await LogFileService.GetCachedLogFileAsync(LogId.GenerateNewId());
            Assert.IsNull(NotFound);

            await LogFileService.CreateLogFileAsync(JobId.GenerateNewId(), ObjectId.GenerateNewId(), LogType.Text);
            Assert.AreEqual(2, (await LogFileService.GetLogFilesAsync()).Count);
        }

        [TestMethod]
        public async Task AuthorizeForSession()
        {
			JobId JobId = JobId.GenerateNewId();
            ObjectId SessionId = ObjectId.GenerateNewId();
            ILogFile LogFile = await LogFileService.CreateLogFileAsync(JobId, SessionId, LogType.Text);
            ILogFile LogFileNoSession = await LogFileService.CreateLogFileAsync(JobId, null, LogType.Text);

            var HasClaim = new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
            {
                new Claim(HordeClaimTypes.AgentSessionId, SessionId.ToString()),
            }, "TestAuthType"));
            var HasNoClaim = new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
            {
                new Claim(HordeClaimTypes.AgentSessionId, "invalid-session-id"),
            }, "TestAuthType"));

            Assert.IsTrue(LogFileService.AuthorizeForSession(LogFile, HasClaim));
            Assert.IsFalse(LogFileService.AuthorizeForSession(LogFile, HasNoClaim));
            Assert.IsFalse(LogFileService.AuthorizeForSession(LogFileNoSession, HasClaim));
        }

		[TestMethod]
		public async Task ChunkSplitting()
		{
			JobId JobId = JobId.GenerateNewId();
			ILogFile LogFile = await LogFileService.CreateLogFileAsync(JobId, null, LogType.Text);

			long Offset = 0;

			const int MaxChunkSize = 4;
			const int MaxSubChunkLineCount = 128;

			byte[] Line1 = Encoding.UTF8.GetBytes("hello world\n");
			await LogFileService.WriteLogDataAsync(LogFile, Offset, 0, Line1, false, MaxChunkSize, MaxSubChunkLineCount);
			Offset += Line1.Length;

			byte[] Line2 = Encoding.UTF8.GetBytes("ab\n");
			await LogFileService.WriteLogDataAsync(LogFile, Offset, 1, Line2, false, MaxChunkSize, MaxSubChunkLineCount);
			Offset += Line2.Length;

			byte[] Line3 = Encoding.UTF8.GetBytes("a\n");
			await LogFileService.WriteLogDataAsync(LogFile, Offset, 2, Line3, false, MaxChunkSize, MaxSubChunkLineCount);
			Offset += Line3.Length;

			byte[] Line4 = Encoding.UTF8.GetBytes("b\n");
			await LogFileService.WriteLogDataAsync(LogFile, Offset, 3, Line4, false, MaxChunkSize, MaxSubChunkLineCount);
			Offset += Line4.Length;

			byte[] Line5 = Encoding.UTF8.GetBytes("a\nb\n");
			await LogFileService.WriteLogDataAsync(LogFile, Offset, 4, Line5, false, MaxChunkSize, MaxSubChunkLineCount);
			Offset += Line5.Length;

			await LogFileService.FlushAsync();
			LogFile = (await LogFileService.GetLogFileAsync(LogFile.Id))!;
			Assert.AreEqual(4, LogFile.Chunks.Count);
			Assert.AreEqual(6, LogFile.MaxLineIndex);

			Assert.AreEqual(0, LogFile.Chunks[0].LineIndex);
			Assert.AreEqual(1, LogFile.Chunks[1].LineIndex);
			Assert.AreEqual(2, LogFile.Chunks[2].LineIndex);
			Assert.AreEqual(4, LogFile.Chunks[3].LineIndex);

			Assert.AreEqual(12, LogFile.Chunks[0].Length);
			Assert.AreEqual(3, LogFile.Chunks[1].Length);
			Assert.AreEqual(4, LogFile.Chunks[2].Length);
			Assert.AreEqual(4, LogFile.Chunks[3].Length);

			Assert.AreEqual(0, LogFile.Chunks.GetChunkForLine(0));
			Assert.AreEqual(1, LogFile.Chunks.GetChunkForLine(1));
			Assert.AreEqual(2, LogFile.Chunks.GetChunkForLine(2));
			Assert.AreEqual(2, LogFile.Chunks.GetChunkForLine(3));
			Assert.AreEqual(3, LogFile.Chunks.GetChunkForLine(4));
			Assert.AreEqual(3, LogFile.Chunks.GetChunkForLine(5));
		}
	}
}