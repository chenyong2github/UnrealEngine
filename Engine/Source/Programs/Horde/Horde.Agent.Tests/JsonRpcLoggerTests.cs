// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Logs;
using Horde.Agent.Parser;
using Horde.Agent.Utility;
using Horde.Common.Rpc;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Agent.Tests
{
	[TestClass]
	public class JsonRpcLoggerTests
	{
		class JsonLoggerImpl : ILogger
		{
			public List<LogEvent> _lines = new List<LogEvent>();

			public IDisposable BeginScope<TState>(TState state) => null!;

			public bool IsEnabled(LogLevel logLevel) => true;

			public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
			{
				JsonLogEvent logEvent = JsonLogEvent.FromLoggerState(logLevel, eventId, state, exception, formatter);
				_lines.Add(LogEvent.Read(logEvent.Data.Span));
			}
		}

		[TestMethod]
		public void JsonLogEventTest()
		{
			JsonLoggerImpl impl = new JsonLoggerImpl();
			impl.LogInformation("Hello {Text}", "world");
			impl.LogInformation("Hello {Text}", "world");

			Assert.AreEqual(impl._lines.Count, 2);
			Assert.AreEqual(impl._lines[0].ToString(), "Hello world");
			Assert.AreEqual(impl._lines[0].GetProperty("Text").ToString()!, "world");
			Assert.AreEqual(impl._lines[1].ToString(), "Hello world");
			Assert.AreEqual(impl._lines[1].GetProperty("Text").ToString(), "world");
		}

		static string[] SplitMultiLineMessage(string message)
		{
			JsonLogEvent jsonLogEvent;
			Assert.IsTrue(JsonLogEvent.TryParse(Encoding.UTF8.GetBytes(message), out jsonLogEvent));
			ArrayBufferWriter<byte> writer = new ArrayBufferWriter<byte>();
			int count = JsonRpcLogger.WriteEvent(jsonLogEvent, writer);
			string[] result = Encoding.UTF8.GetString(writer.WrittenSpan).Split('\n', StringSplitOptions.RemoveEmptyEntries);
			Assert.AreEqual(count, result.Length);
			return result;
		}

		[TestMethod]
		public void MultiLineFormatTest()
		{
			const string msg = @"{""level"":""Information"",""message"":""ignored"",""format"":""This\nis a\nmulti-{Line}-end\nlog message {Var1} {Var2}"",""properties"":{""Line"":""line\nsplit\nin\nfour"",""Var1"":123,""Var2"":{""$type"":""SourceFile"",""$text"":""D:\\build\\\u002B\u002BUE5\\Sync\\GenerateProjectFiles.bat"",""relativePath"":""GenerateProjectFiles.bat"",""depotPath"":""//UE5/Main/GenerateProjectFiles.bat@20392842""}}}";

			string[] result = SplitMultiLineMessage(msg);
			Assert.AreEqual(7, result.Length);
			Assert.AreEqual(@"{""level"":""Information"",""message"":""This"",""format"":""This"",""properties"":{""Line"":""line\nsplit\nin\nfour"",""Var1"":123,""Var2"":{""$type"":""SourceFile"",""$text"":""D:\\build\\\u002B\u002BUE5\\Sync\\GenerateProjectFiles.bat"",""relativePath"":""GenerateProjectFiles.bat"",""depotPath"":""//UE5/Main/GenerateProjectFiles.bat@20392842""},""Line$0"":""line"",""Line$1"":""split"",""Line$2"":""in"",""Line$3"":""four""},""line"":0,""lineCount"":7}", result[0]);
			Assert.AreEqual(@"{""level"":""Information"",""message"":""is a"",""format"":""is a"",""properties"":{""Line"":""line\nsplit\nin\nfour"",""Var1"":123,""Var2"":{""$type"":""SourceFile"",""$text"":""D:\\build\\\u002B\u002BUE5\\Sync\\GenerateProjectFiles.bat"",""relativePath"":""GenerateProjectFiles.bat"",""depotPath"":""//UE5/Main/GenerateProjectFiles.bat@20392842""},""Line$0"":""line"",""Line$1"":""split"",""Line$2"":""in"",""Line$3"":""four""},""line"":1,""lineCount"":7}", result[1]);
			Assert.AreEqual(@"{""level"":""Information"",""message"":""multi-line"",""format"":""multi-{Line$0}"",""properties"":{""Line"":""line\nsplit\nin\nfour"",""Var1"":123,""Var2"":{""$type"":""SourceFile"",""$text"":""D:\\build\\\u002B\u002BUE5\\Sync\\GenerateProjectFiles.bat"",""relativePath"":""GenerateProjectFiles.bat"",""depotPath"":""//UE5/Main/GenerateProjectFiles.bat@20392842""},""Line$0"":""line"",""Line$1"":""split"",""Line$2"":""in"",""Line$3"":""four""},""line"":2,""lineCount"":7}", result[2]);
			Assert.AreEqual(@"{""level"":""Information"",""message"":""split"",""format"":""{Line$1}"",""properties"":{""Line"":""line\nsplit\nin\nfour"",""Var1"":123,""Var2"":{""$type"":""SourceFile"",""$text"":""D:\\build\\\u002B\u002BUE5\\Sync\\GenerateProjectFiles.bat"",""relativePath"":""GenerateProjectFiles.bat"",""depotPath"":""//UE5/Main/GenerateProjectFiles.bat@20392842""},""Line$0"":""line"",""Line$1"":""split"",""Line$2"":""in"",""Line$3"":""four""},""line"":3,""lineCount"":7}", result[3]);
			Assert.AreEqual(@"{""level"":""Information"",""message"":""in"",""format"":""{Line$2}"",""properties"":{""Line"":""line\nsplit\nin\nfour"",""Var1"":123,""Var2"":{""$type"":""SourceFile"",""$text"":""D:\\build\\\u002B\u002BUE5\\Sync\\GenerateProjectFiles.bat"",""relativePath"":""GenerateProjectFiles.bat"",""depotPath"":""//UE5/Main/GenerateProjectFiles.bat@20392842""},""Line$0"":""line"",""Line$1"":""split"",""Line$2"":""in"",""Line$3"":""four""},""line"":4,""lineCount"":7}", result[4]);
			Assert.AreEqual(@"{""level"":""Information"",""message"":""four-end"",""format"":""{Line$3}-end"",""properties"":{""Line"":""line\nsplit\nin\nfour"",""Var1"":123,""Var2"":{""$type"":""SourceFile"",""$text"":""D:\\build\\\u002B\u002BUE5\\Sync\\GenerateProjectFiles.bat"",""relativePath"":""GenerateProjectFiles.bat"",""depotPath"":""//UE5/Main/GenerateProjectFiles.bat@20392842""},""Line$0"":""line"",""Line$1"":""split"",""Line$2"":""in"",""Line$3"":""four""},""line"":5,""lineCount"":7}", result[5]);
			Assert.AreEqual(@"{""level"":""Information"",""message"":""log message 123 D:\\build\\\u002B\u002BUE5\\Sync\\GenerateProjectFiles.bat"",""format"":""log message {Var1} {Var2}"",""properties"":{""Line"":""line\nsplit\nin\nfour"",""Var1"":123,""Var2"":{""$type"":""SourceFile"",""$text"":""D:\\build\\\u002B\u002BUE5\\Sync\\GenerateProjectFiles.bat"",""relativePath"":""GenerateProjectFiles.bat"",""depotPath"":""//UE5/Main/GenerateProjectFiles.bat@20392842""},""Line$0"":""line"",""Line$1"":""split"",""Line$2"":""in"",""Line$3"":""four""},""line"":6,""lineCount"":7}", result[6]);
		}

		[TestMethod]
		public void MultiLineMessageTest()
		{
			const string msg = @"{""level"":""Information"",""message"":""This is \na multi-line string""}";

			string[] result = SplitMultiLineMessage(msg);
			Assert.AreEqual(2, result.Length);
			Assert.AreEqual(result[0], @"{""level"":""Information"",""message"":""This is "",""line"":0,""lineCount"":2}");
			Assert.AreEqual(result[1], @"{""level"":""Information"",""message"":""a multi-line string"",""line"":1,""lineCount"":2}");
		}

		[TestMethod]
		public void MultiLineDecoyTest()
		{
			const string msg = @"{""level"":""Information"",""message"":""This is \\\\not a multi-line format string""}";

			string[] result = SplitMultiLineMessage(msg);
			Assert.AreEqual(1, result.Length);
			Assert.AreEqual(result[0], msg);
		}

		class FakeJsonRpcLoggerBackend : JsonRpcAndStorageLogSink
		{
			public NodeHandle? Target { get; private set; }

			public FakeJsonRpcLoggerBackend(IRpcConnection connection, string logId, IJsonRpcLogSink inner, IStorageClient store, ILogger logger)
				: base(connection, logId, inner, store, logger)
			{
			}

			protected override Task UpdateLogAsync(NodeHandle target, int lineCount, CancellationToken cancellationToken)
			{
				Target = target;
				return Task.CompletedTask;
			}

			protected override async Task<int> UpdateLogTailAsync(int tailNext, ReadOnlyMemory<byte> tailData)
			{
				await Task.Delay(TimeSpan.FromSeconds(2.0));
				return -1;
			}
		}

		class FakeLogSink : IJsonRpcLogSink
		{
			public ValueTask DisposeAsync() => new ValueTask();

			public Task SetOutcomeAsync(JobStepOutcome outcome, CancellationToken cancellationToken) => Task.CompletedTask;

			public Task WriteEventsAsync(List<CreateEventRequest> events, CancellationToken cancellationToken) => Task.CompletedTask;

			public Task WriteOutputAsync(WriteOutputRequest request, CancellationToken cancellationToken) => Task.CompletedTask;
		}

		[TestMethod]
		public async Task StorageLoggerTest()
		{
			using MemoryCache cache = new MemoryCache(new MemoryCacheOptions());
			MemoryStorageClient store = new MemoryStorageClient();

			TreeReader reader = new TreeReader(store, cache, NullLogger.Instance);

			await using FakeLogSink innerSink = new FakeLogSink();

			const int Count = 20000;

			LogNode file;
			await using (FakeJsonRpcLoggerBackend sink = new FakeJsonRpcLoggerBackend(null!, "foo", innerSink, store, NullLogger.Instance))
			{
				await using (JsonRpcLogger logger = new JsonRpcLogger(sink, "foo", null, NullLogger.Instance))
				{
					for (int idx = 0; idx < Count; idx++)
					{
						logger.LogInformation("Testing {Number}", idx);
					}
				}
				file = await reader.ReadNodeAsync<LogNode>(sink.Target!.Locator!);
			}

			// Check the index text
			List<Utf8String> extractedIndexText = new List<Utf8String>();

			LogIndexNode index = await file.IndexRef.ExpandAsync(reader);
			foreach (LogChunkRef block in index.PlainTextChunkRefs)
			{
				LogChunkNode text = await block.ExpandAsync(reader);
				extractedIndexText.AddRange(text.Lines);
			}

			Assert.AreEqual(Count, extractedIndexText.Count);
			for (int idx = 0; idx < Count; idx++)
			{
				Assert.AreEqual(extractedIndexText[idx], new Utf8String($"Testing {idx}"));
			}

			// Check the body text
			List<string> extractedBodyText = new List<string>();

			foreach (LogChunkRef blockRef in file.TextChunkRefs)
			{
				LogChunkNode blockText = await blockRef.ExpandAsync(reader);
				foreach (Utf8String line in blockText.Lines)
				{
					LogEvent logEvent = LogEvent.Read(line.Span);
					extractedBodyText.Add(logEvent.ToString());
				}
			}

			Assert.AreEqual(Count, extractedBodyText.Count);
			for (int idx = 0; idx < Count; idx++)
			{
				Assert.AreEqual(extractedBodyText[idx], $"Testing {idx}");
			}
		}
	}
}
