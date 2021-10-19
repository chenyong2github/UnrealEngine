// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Logs;
using HordeServer.Logs.Builder;
using HordeServer.Logs.Storage;
using HordeServer.Logs.Storage.Impl;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using HordeServer.Compute.Tests.Properties;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using HordeServer.Collections.Impl;

namespace HordeServerTests
{
	using JobId = ObjectId<IJob>;

	[TestClass]
	public class LogIndexingTests : DatabaseIntegrationTest
	{
		private readonly ILogFileService LogFileService;
		private byte[] Data = Resources.TextFile;

		public LogIndexingTests()
		{
			LogFileCollection LogFileCollection = new LogFileCollection(GetDatabaseService());

			ServiceProvider ServiceProvider = new ServiceCollection()
				.AddLogging(Builder => Builder.AddConsole().SetMinimumLevel(LogLevel.Debug))
				.BuildServiceProvider();

			ILoggerFactory LoggerFactory = ServiceProvider.GetRequiredService<ILoggerFactory>();
			ILogger<LogFileService> Logger = LoggerFactory.CreateLogger<LogFileService>();

			// Just to satisfy the parameter, need to be fixed
			IConfiguration Config = new ConfigurationBuilder().Build();

			ILogBuilder LogBuilder = new LocalLogBuilder();
			ILogStorage LogStorage = new LocalLogStorage(20, new NullLogStorage());
			LogFileService = new LogFileService(LogFileCollection, null!, LogBuilder, LogStorage, Logger);
		}

		[TestMethod]
		public async Task IndexTests()
		{
			JobId JobId = JobId.GenerateNewId();
			ILogFile LogFile = await LogFileService.CreateLogFileAsync(JobId, null, LogType.Text);

			// Write the test data to the log file in blocks
			int Offset = 0;
			int LineIndex = 0;
			while (Offset < Data.Length)
			{
				int Length = 0;
				int LineCount = 0;

				for(int Idx = Offset; Length == 0 || Idx < Math.Min(Data.Length, Offset + 1883); Idx++)
				{
					if(Data[Idx] == '\n')
					{
						Length = (Idx + 1) - Offset;
						LineCount++;
						break;
					}
				}

				LogFile = await WriteLogDataAsync(LogFile, Offset, LineIndex, Data.AsMemory(Offset, Length), false);

				Offset += Length;
				LineIndex += LineCount;
			}

			// Read the data back out and check it's the same
			byte[] ReadData = new byte[Data.Length];
			using (Stream Stream = await LogFileService.OpenRawStreamAsync(LogFile, 0, Data.Length))
			{
				int ReadSize = await Stream.ReadAsync(ReadData, 0, ReadData.Length);
				Assert.AreEqual(ReadData.Length, ReadSize);

				int EqualSize = 0;
				while(EqualSize < Data.Length && Data[EqualSize] == ReadData[EqualSize])
				{
					EqualSize++;
				}

				Assert.AreEqual(EqualSize, ReadSize);
			}

			// Test some searches
			await SearchLogDataTestAsync(LogFile);

			// Generate an index and test again
			LogFile = await WriteLogDataAsync(LogFile, Offset, LineIndex, Array.Empty<byte>(), true);
			await SearchLogDataTestAsync(LogFile);
		}

		[TestMethod]
		public void TrieTests()
		{
			ReadOnlyTrieBuilder Builder = new ReadOnlyTrieBuilder();

			ulong[] Values = { 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987, 1597, 2584, 4181, 6765, 10946, 0xfedcba9876543210UL };
			foreach (ulong Value in Values)
			{
				Builder.Add(Value);
			}

			ReadOnlyTrie Trie = Builder.Build();
			Assert.IsTrue(Enumerable.SequenceEqual(Trie.EnumerateRange(0, ulong.MaxValue), Values));
			Assert.IsTrue(Enumerable.SequenceEqual(Trie.EnumerateRange(0, 90), Values.Where(x => x <= 90)));
			Assert.IsTrue(Enumerable.SequenceEqual(Trie.EnumerateRange(2, 89), Values.Where(x => x >= 2 && x <= 89)));
		}

		[TestMethod]
		public async Task PartialTokenTests()
		{
			JobId JobId = JobId.GenerateNewId();
			ILogFile LogFile = await LogFileService.CreateLogFileAsync(JobId, null, LogType.Text);

			string[] Lines =
			{
				"abcdefghi\n",
				"jklmno123\n",
				"pqrst99uv\n",
				"wx\n"
			};

			int Length = 0;
			for (int LineIdx = 0; LineIdx < Lines.Length; LineIdx++)
			{
				LogFile = await WriteLogDataAsync(LogFile, Length, LineIdx, Encoding.UTF8.GetBytes(Lines[LineIdx]), true);
				Length += Lines[LineIdx].Length;
			}

			for (int LineIdx = 0; LineIdx < Lines.Length; LineIdx++)
			{
				for (int StrLen = 1; StrLen < 7; StrLen++)
				{
					for (int StrOfs = 0; StrOfs + StrLen < Lines[LineIdx].Length - 1; StrOfs++)
					{
						string Str = Lines[LineIdx].Substring(StrOfs, StrLen);

						LogSearchStats Stats = new LogSearchStats();
						List<int> Results = await LogFileService.SearchLogDataAsync(LogFile, Str, 0, 5, Stats);
						Assert.AreEqual(1, Results.Count);
						Assert.AreEqual(LineIdx, Results[0]);

						Assert.AreEqual(1, Stats.NumScannedBlocks);
						Assert.AreEqual(3, Stats.NumSkippedBlocks);
						Assert.AreEqual(0, Stats.NumFalsePositiveBlocks);
					}
				}
			}
		}

		[TestMethod]
		public async Task AppendIndexTests()
		{
			JobId JobId = JobId.GenerateNewId();
			ILogFile LogFile = await LogFileService.CreateLogFileAsync(JobId, null, LogType.Text);

			LogFile = await WriteLogDataAsync(LogFile, 0, 0, Encoding.UTF8.GetBytes("abc\n"), true);
			LogFile = await WriteLogDataAsync(LogFile, 4, 1, Encoding.UTF8.GetBytes("def\n"), true);
			LogFile = await WriteLogDataAsync(LogFile, 8, 2, Encoding.UTF8.GetBytes("ghi\n"), false);

			await ((LogFileService)LogFileService).FlushPendingWritesAsync();

			{
				LogSearchStats Stats = new LogSearchStats();
				List<int> Results = await LogFileService.SearchLogDataAsync(LogFile, "abc", 0, 5, Stats);
				Assert.AreEqual(1, Results.Count);
				Assert.AreEqual(0, Results[0]);

				Assert.AreEqual(2, Stats.NumScannedBlocks); // abc + ghi (no index yet because it hasn't been flushed)
				Assert.AreEqual(1, Stats.NumSkippedBlocks); // def
				Assert.AreEqual(0, Stats.NumFalsePositiveBlocks);
			}
			{
				LogSearchStats Stats = new LogSearchStats();
				List<int> Results = await LogFileService.SearchLogDataAsync(LogFile, "def", 0, 5, Stats);
				Assert.AreEqual(1, Results.Count);
				Assert.AreEqual(1, Results[0]);

				Assert.AreEqual(2, Stats.NumScannedBlocks); // def + ghi (no index yet because it hasn't been flushed)
				Assert.AreEqual(1, Stats.NumSkippedBlocks); // abc
				Assert.AreEqual(0, Stats.NumFalsePositiveBlocks);
			}
			{
				LogSearchStats Stats = new LogSearchStats();
				List<int> Results = await LogFileService.SearchLogDataAsync(LogFile, "ghi", 0, 5, Stats);
				Assert.AreEqual(1, Results.Count);
				Assert.AreEqual(2, Results[0]);

				Assert.AreEqual(1, Stats.NumScannedBlocks); // ghi
				Assert.AreEqual(2, Stats.NumSkippedBlocks); // abc + def
				Assert.AreEqual(0, Stats.NumFalsePositiveBlocks);
			}
		}

		async Task<ILogFile> WriteLogDataAsync(ILogFile LogFile, long Offset, int LineIndex, ReadOnlyMemory<byte> Data, bool Flush)
		{
			const int MaxChunkLength = 32 * 1024;
			const int MaxSubChunkLineCount = 128;

			ILogFile? NewLogFile = await LogFileService.WriteLogDataAsync(LogFile, Offset, LineIndex, Data, Flush, MaxChunkLength, MaxSubChunkLineCount);
			Assert.IsNotNull(NewLogFile);
			return NewLogFile!;
		}

		async Task SearchLogDataTestAsync(ILogFile LogFile)
		{
			await SearchLogDataTestAsync(LogFile, "HISPANIOLA", 0, 4, new[] { 1503, 1520, 1525, 1595 });
			await SearchLogDataTestAsync(LogFile, "Hispaniola", 0, 4, new[] { 1503, 1520, 1525, 1595 });
			await SearchLogDataTestAsync(LogFile, "HizpaniolZ", 0, 4, Array.Empty<int>());
			await SearchLogDataTestAsync(LogFile, "Pieces of eight!", 0, 100, new[] { 2227, 2228, 5840, 5841, 7520 });
			await SearchLogDataTestAsync(LogFile, "NEWSLETTER", 0, 100, new[] { 7886 });
		}

		async Task SearchLogDataTestAsync(ILogFile LogFile, string Text, int FirstLine, int Count, int[] ExpectedLines)
		{
			LogSearchStats Stats = new LogSearchStats();
			List<int> Lines = await LogFileService.SearchLogDataAsync(LogFile, Text, FirstLine, Count, Stats);
			Assert.IsTrue(Lines.SequenceEqual(ExpectedLines));
		}
	}
}
