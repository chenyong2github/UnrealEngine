// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Logs;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using Google.Protobuf;
using Grpc.Core;
using Horde.Agent.Services;
using Horde.Agent.Utility;
using Horde.Common.Rpc;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;

using static Horde.Common.Rpc.LogRpc;

namespace Horde.Agent.Parser
{
	using JsonObject = System.Text.Json.Nodes.JsonObject;

	interface IJsonRpcLogSink : IAsyncDisposable
	{
		Task WriteEventsAsync(List<CreateEventRequest> events, CancellationToken cancellationToken);
		Task WriteOutputAsync(WriteOutputRequest request, CancellationToken cancellationToken);
		Task SetOutcomeAsync(JobStepOutcome outcome, CancellationToken cancellationToken);
	}

	sealed class JsonRpcLogSink : IJsonRpcLogSink
	{
		readonly IRpcConnection _rpcClient;
		readonly string? _jobId;
		readonly string? _jobBatchId;
		readonly string? _jobStepId;
		readonly ILogger _logger;

		public JsonRpcLogSink(IRpcConnection rpcClient, string? jobId, string? jobBatchId, string? jobStepId, ILogger logger)
		{
			_rpcClient = rpcClient;
			_jobId = jobId;
			_jobBatchId = jobBatchId;
			_jobStepId = jobStepId;
			_logger = logger;
		}

		public ValueTask DisposeAsync() => new ValueTask();

		/// <inheritdoc/>
		public async Task WriteEventsAsync(List<CreateEventRequest> events, CancellationToken cancellationToken)
		{
			await _rpcClient.InvokeAsync((HordeRpc.HordeRpcClient x) => x.CreateEventsAsync(new CreateEventsRequest(events)), cancellationToken);
		}

		/// <inheritdoc/>
		public async Task WriteOutputAsync(WriteOutputRequest request, CancellationToken cancellationToken)
		{
			await _rpcClient.InvokeAsync((HordeRpc.HordeRpcClient x) => x.WriteOutputAsync(request), cancellationToken);
		}

		/// <inheritdoc/>
		public async Task SetOutcomeAsync(JobStepOutcome outcome, CancellationToken cancellationToken)
		{
			// Update the outcome of this jobstep
			if (_jobId != null && _jobBatchId != null && _jobStepId != null)
			{
				try
				{
					await _rpcClient.InvokeAsync((HordeRpc.HordeRpcClient x) => x.UpdateStepAsync(new UpdateStepRequest(_jobId, _jobBatchId, _jobStepId, JobStepState.Unspecified, outcome)), cancellationToken);
				}
				catch (Exception ex)
				{
					_logger.LogWarning(ex, "Unable to update step outcome to {NewOutcome}", outcome);
				}
			}
		}
	}

	class JsonRpcAndStorageLogSink : IJsonRpcLogSink, IAsyncDisposable
	{
		const int FlushLength = 1024 * 1024;

		readonly IRpcConnection _connection;
		readonly string _logId;
		readonly LogBuilder _builder;
		readonly IJsonRpcLogSink? _inner;
		readonly TreeWriter _writer;
		readonly ILogger _logger;

		int _bufferLength;

		// Background task
		readonly object _lockObject = new object();

		// Tailing task
		readonly Task _tailTask;
		AsyncEvent _tailTaskStop;
		readonly AsyncEvent _newTailDataEvent = new AsyncEvent();

		public JsonRpcAndStorageLogSink(IRpcConnection connection, string logId, IJsonRpcLogSink? inner, IStorageClient store, ILogger logger)
		{
			_connection = connection;
			_logId = logId;
			_builder = new LogBuilder(LogFormat.Json, logger);
			_inner = inner;
			_writer = new TreeWriter(store);
			_logger = logger;

			_tailTaskStop = new AsyncEvent();
			_tailTask = Task.Run(() => TickTailAsync());
		}

		public async ValueTask DisposeAsync()
		{
			if(_tailTaskStop != null)
			{
				_tailTaskStop.Latch();
				try
				{
					await _tailTask;
				}
				catch (OperationCanceledException)
				{
				}
				catch (Exception ex)
				{
					_logger.LogError(ex, "Exception on log tailing task ({LogId}): {Message}", _logId, ex.Message);
				}
				_tailTaskStop = null!;
			}

			if (_inner != null)
			{
				await _inner.DisposeAsync();
			}

			if (_writer != null)
			{
				_writer.Dispose();
			}
		}

		async Task TickTailAsync()
		{
			int tailNext = -1;
			while (!_tailTaskStop.IsSet())
			{
				Task newTailDataTask;

				// Get the data to send to the server
				ReadOnlyMemory<byte> tailData = ReadOnlyMemory<byte>.Empty;
				lock (_lockObject)
				{
					if (tailNext != -1)
					{
						tailNext = Math.Max(tailNext, _builder.FlushedLineCount);
						tailData = _builder.ReadTailData(tailNext, 16 * 1024);
					}
					newTailDataTask = _newTailDataEvent.Task;
				}

				// If we don't have any updates for the server, wait until we do.
				if (tailNext != -1 && tailData.IsEmpty)
				{
					await newTailDataTask;
					continue;
				}

				// Update the next tailing position
				int newTailNext = await UpdateLogTailAsync(tailNext, tailData);
				if (newTailNext != tailNext)
				{
					tailNext = newTailNext;
					_logger.LogInformation("Modified tail position for log {LogId} to {TailNext}", _logId, tailNext);
				}
			}
		}

		/// <inheritdoc/>
		public async Task SetOutcomeAsync(JobStepOutcome outcome, CancellationToken cancellationToken)
		{
			if (_inner != null)
			{
				await _inner.SetOutcomeAsync(outcome, cancellationToken);
			}
		}

		/// <inheritdoc/>
		public async Task WriteEventsAsync(List<CreateEventRequest> events, CancellationToken cancellationToken)
		{
			if (_inner != null)
			{
				await _inner.WriteEventsAsync(events, cancellationToken);
			}
		}

		/// <inheritdoc/>
		public async Task WriteOutputAsync(WriteOutputRequest request, CancellationToken cancellationToken)
		{
			if (_inner != null)
			{
				await _inner.WriteOutputAsync(request, cancellationToken);
			}

			_builder.WriteData(request.Data.Memory);
			_bufferLength += request.Data.Length;

			if (request.Flush || _bufferLength > FlushLength)
			{
				NodeHandle target = await _builder.FlushAsync(_writer, request.Flush, cancellationToken);
				await UpdateLogAsync(target, _builder.LineCount, cancellationToken);
				_bufferLength = 0;
			}

			_newTailDataEvent.Set();
		}

		#region RPC calls

		protected virtual async Task UpdateLogAsync(NodeHandle target, int lineCount, CancellationToken cancellationToken)
		{
			_logger.LogDebug("Updating log {LogId} to line {LineCount}, target {Locator}", _logId, lineCount, target);

			UpdateLogRequest request = new UpdateLogRequest();
			request.LogId = _logId;
			request.LineCount = lineCount;
			request.Target = target.ToString();
			await _connection.InvokeAsync((LogRpcClient client) => client.UpdateLogAsync(request, cancellationToken: cancellationToken), cancellationToken);
		}

		protected virtual async Task<int> UpdateLogTailAsync(int tailNext, ReadOnlyMemory<byte> tailData)
		{
			DateTime deadline = DateTime.UtcNow.AddMinutes(2.0);
			using (IRpcClientRef<LogRpcClient> clientRef = await _connection.GetClientRefAsync<LogRpcClient>(CancellationToken.None))
			{
				using (AsyncDuplexStreamingCall<UpdateLogTailRequest, UpdateLogTailResponse> call = clientRef.Client.UpdateLogTail(deadline: deadline))
				{
					// Write the request to the server
					UpdateLogTailRequest request = new UpdateLogTailRequest();
					request.LogId = _logId;
					request.TailNext = tailNext;
					request.TailData = UnsafeByteOperations.UnsafeWrap(tailData);
					await call.RequestStream.WriteAsync(request);

					// Wait until the server responds or we need to trigger a new update
					Task<bool> moveNextAsync = call.ResponseStream.MoveNext();

					Task task = await Task.WhenAny(moveNextAsync, clientRef.DisposingTask, _tailTaskStop.Task, Task.Delay(TimeSpan.FromMinutes(1.0), CancellationToken.None));
					if (task == clientRef.DisposingTask)
					{
						_logger.LogDebug("Cancelling long poll from client side (server migration)");
					}
					else if (task == _tailTaskStop.Task)
					{
						_logger.LogDebug("Cancelling long poll from client side (complete)");
					}

					// Close the request stream to indicate that we're finished
					await call.RequestStream.CompleteAsync();

					// Wait for a response or a new update to come in, then close the request stream
					UpdateLogTailResponse? response = null;
					while (await moveNextAsync)
					{
						response = call.ResponseStream.Current;
						moveNextAsync = call.ResponseStream.MoveNext();
					}
					return response?.TailNext ?? -1;
				}
			}
		}

		#endregion
	}

	/// <summary>
	/// Interface for a log device
	/// </summary>
	public interface IServerLogger : ILogger, IAsyncDisposable
	{
		/// <summary>
		/// Outcome of the job step, including any warnings/errors
		/// </summary>
		JobStepOutcome Outcome { get; }

		/// <summary>
		/// Flushes the logger with the server and stops the background work
		/// </summary>
		Task StopAsync();
	}

	/// <summary>
	/// Class to handle uploading log data to the server in the background
	/// </summary>
	sealed class JsonRpcLogger : IServerLogger
	{
		class QueueItem
		{
			public byte[] Data { get; }
			public CreateEventRequest? CreateEvent { get; }

			public QueueItem(byte[] data, CreateEventRequest? createEvent)
			{
				Data = data;
				CreateEvent = createEvent;
			}

			public override string ToString()
			{
				return Encoding.UTF8.GetString(Data);
			}
		}

		internal readonly IJsonRpcLogSink _sink;
		internal readonly string _logId;
		internal readonly bool _warnings;
		internal readonly ILogger _inner;
		readonly Channel<JsonLogEvent> _dataChannel;
		Task? _dataWriter;

		/// <summary>
		/// The current outcome for this step. Updated to reflect any errors and warnings that occurred.
		/// </summary>
		public JobStepOutcome Outcome
		{
			get;
			private set;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="sink">Sink for log events</param>
		/// <param name="logId">The log id to write to</param>
		/// <param name="warnings">Whether to include warnings in the output</param>
		/// <param name="inner">Additional logger to write to</param>
		public JsonRpcLogger(IJsonRpcLogSink sink, string logId, bool? warnings, ILogger inner)
		{
			_sink = sink;
			_logId = logId;
			_warnings = warnings ?? true;
			_inner = inner;
			_dataChannel = Channel.CreateUnbounded<JsonLogEvent>();
			_dataWriter = Task.Run(() => RunDataWriter());

			Outcome = JobStepOutcome.Success;
		}

		/// <inheritdoc/>
		public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
		{
			// Downgrade warnings to information if not required
			if (logLevel == LogLevel.Warning && !_warnings)
			{
				logLevel = LogLevel.Information;
			}

			JsonLogEvent jsonLogEvent = JsonLogEvent.FromLoggerState(logLevel, eventId, state, exception, formatter);
			WriteFormattedEvent(jsonLogEvent);
		}

		/// <inheritdoc/>
		public bool IsEnabled(LogLevel logLevel) => _inner.IsEnabled(logLevel);

		/// <inheritdoc/>
		public IDisposable BeginScope<TState>(TState state) => _inner.BeginScope(state);

		private void WriteFormattedEvent(JsonLogEvent jsonLogEvent)
		{
			// Update the state of this job if this is an error status
			LogLevel level = jsonLogEvent.Level;
			if (level == LogLevel.Error || level == LogLevel.Critical)
			{
				Outcome = JobStepOutcome.Failure;
			}
			else if (level == LogLevel.Warning && Outcome != JobStepOutcome.Failure)
			{
				Outcome = JobStepOutcome.Warnings;
			}

			// Write the event
			if (!_dataChannel.Writer.TryWrite(jsonLogEvent))
			{
				throw new InvalidOperationException("Expected unbounded writer to complete immediately");
			}
		}

		/// <summary>
		/// Stops the log writer's background task
		/// </summary>
		/// <returns>Async task</returns>
		public async Task StopAsync()
		{
			if (_dataWriter != null)
			{
				_dataChannel.Writer.TryComplete();
				await _dataWriter;
				_dataWriter = null;
			}
		}

		/// <summary>
		/// Dispose of this object. Call StopAsync() to stop asynchronously.
		/// </summary>
		public async ValueTask DisposeAsync()
		{
			await StopAsync();
			await _sink.DisposeAsync();
		}

		/// <summary>
		/// Upload the log data to the server in the background
		/// </summary>
		/// <returns>Async task</returns>
		async Task RunDataWriter()
		{
			// Current position and line number in the log file
			long offset = 0;
			int lineIndex = 0;

			// Total number of errors and warnings
			const int MaxErrors = 50;
			int numErrors = 0;
			const int MaxWarnings = 50;
			int numWarnings = 0;

			// Buffers for chunks and events read in a single iteration
			ArrayBufferWriter<byte> writer = new ArrayBufferWriter<byte>();
			List<CreateEventRequest> events = new List<CreateEventRequest>();

			// The current jobstep outcome
			JobStepOutcome postedOutcome = JobStepOutcome.Success;

			// Whether we've written the flush command
			for (; ; )
			{
				writer.Clear();
				events.Clear();

				// Save off the current line number for sending to the server
				int initialLineIndex = lineIndex;

				// Get the next data
				Task waitTask = Task.Delay(TimeSpan.FromSeconds(2.0));
				while (writer.WrittenCount < 256 * 1024)
				{
					JsonLogEvent jsonLogEvent;
					if (_dataChannel.Reader.TryRead(out jsonLogEvent))
					{
						int lineCount = WriteEvent(jsonLogEvent, writer);
						if (jsonLogEvent.LineIndex == 0)
						{
							if (jsonLogEvent.Level == LogLevel.Warning && ++numWarnings <= MaxWarnings)
							{
								AddEvent(jsonLogEvent.Data.Span, lineIndex, Math.Max(lineCount, jsonLogEvent.LineCount), EventSeverity.Warning, events);
							}
							else if ((jsonLogEvent.Level == LogLevel.Error || jsonLogEvent.Level == LogLevel.Critical) && ++numErrors <= MaxErrors)
							{
								AddEvent(jsonLogEvent.Data.Span, lineIndex, Math.Max(lineCount, jsonLogEvent.LineCount), EventSeverity.Error, events);
							}
						}
						lineIndex += lineCount;
					}
					else
					{
						Task<bool> readTask = _dataChannel.Reader.WaitToReadAsync().AsTask();
						if (await Task.WhenAny(readTask, waitTask) == waitTask)
						{
							break;
						}
						if (!await readTask)
						{
							break;
						}
					}
				}

				// Upload it to the server
				if (writer.WrittenCount > 0)
				{
					byte[] data = writer.WrittenSpan.ToArray();
					try
					{
						await _sink.WriteOutputAsync(new WriteOutputRequest(_logId, offset, initialLineIndex, data, false), CancellationToken.None);
					}
					catch (Exception ex)
					{
						_inner.LogWarning(ex, "Unable to write data to server (log {LogId}, offset {Offset}, length {Length}, lines {StartLine}-{EndLine})", _logId, offset, data.Length, initialLineIndex, lineIndex);
					}
					offset += data.Length;
				}

				// Write all the events
				if (events.Count > 0)
				{
					try
					{
						await _sink.WriteEventsAsync(events, CancellationToken.None);
					}
					catch (Exception ex)
					{
						_inner.LogWarning(ex, "Unable to create events");
					}
				}

				// Update the outcome of this jobstep
				if (Outcome != postedOutcome)
				{
					try
					{
						await _sink.SetOutcomeAsync(Outcome, CancellationToken.None);
					}
					catch (Exception ex)
					{
						_inner.LogWarning(ex, "Unable to update step outcome to {NewOutcome}", Outcome);
					}
					postedOutcome = Outcome;
				}

				// Wait for more data to be available
				if (!await _dataChannel.Reader.WaitToReadAsync())
				{
					try
					{
						await _sink.WriteOutputAsync(new WriteOutputRequest(_logId, offset, lineIndex, Array.Empty<byte>(), true), CancellationToken.None);
					}
					catch (Exception ex)
					{
						_inner.LogWarning(ex, "Unable to flush data to server (log {LogId}, offset {Offset})", _logId, offset);
					}
					break;
				}
			}
		}

		static readonly string s_messagePropertyName = LogEventPropertyName.Message.ToString();
		static readonly string s_formatPropertyName = LogEventPropertyName.Format.ToString();
		static readonly string s_linePropertyName = LogEventPropertyName.Line.ToString();
		static readonly string s_lineCountPropertyName = LogEventPropertyName.LineCount.ToString();

		static readonly Utf8String s_newline = "\n";
		static readonly Utf8String s_escapedNewline = "\\n";

		public static int WriteEvent(JsonLogEvent jsonLogEvent, IBufferWriter<byte> writer)
		{
			ReadOnlySpan<byte> span = jsonLogEvent.Data.Span;
			if (jsonLogEvent.LineCount == 1 && span.IndexOf(s_escapedNewline) != -1)
			{
				JsonObject obj = (JsonObject)JsonNode.Parse(span)!;

				JsonValue? formatValue = obj["format"] as JsonValue;
				if (formatValue != null && formatValue.TryGetValue(out string? format))
				{
					return WriteEventWithFormat(obj, format, writer);
				}

				JsonValue? messageValue = obj["message"] as JsonValue;
				if (messageValue != null && messageValue.TryGetValue(out string? message))
				{
					return WriteEventWithMessage(obj, message, writer);
				}
			}

			writer.Write(span);
			writer.Write(s_newline);
			return 1;
		}

		static int WriteEventWithFormat(JsonObject obj, string format, IBufferWriter<byte> writer)
		{
			IEnumerable<KeyValuePair<string, object?>> propertyValueList = Enumerable.Empty<KeyValuePair<string, object?>>();

			// Split all the multi-line properties into separate properties
			JsonObject? properties = obj["properties"] as JsonObject;
			if (properties != null)
			{
				// Get all the current property values
				Dictionary<string, string> propertyValues = new Dictionary<string, string>(StringComparer.Ordinal);
				foreach ((string name, JsonNode? node) in properties)
				{
					string value = String.Empty;
					if (node != null)
					{
						if (node is JsonObject valueObject)
						{
							value = valueObject["$text"]?.ToString() ?? String.Empty;
						}
						else
						{
							value = node.ToString();
						}
					}
					propertyValues[name] = value;
				}

				// Split all the multi-line properties into separate things
				int nameStart = -1;
				for (int idx = 0; idx < format.Length; idx++)
				{
					if (format[idx] == '{')
					{
						nameStart = idx + 1;
					}
					else if (format[idx] == '}' && nameStart != -1)
					{
						string name = format.Substring(nameStart, idx - nameStart);
						if (propertyValues.TryGetValue(name, out string? text))
						{
							int textLineEnd = text.IndexOf('\n', StringComparison.Ordinal);
							if (textLineEnd != -1)
							{
								int lineNum = 0;

								StringBuilder builder = new StringBuilder();
								builder.Append(format, 0, nameStart - 1);

								string delimiter = String.Empty;
								for (int textLineStart = 0; textLineStart < text.Length;)
								{
									string newName = $"{name}${lineNum++}";
									string newLine = text.Substring(textLineStart, textLineEnd - textLineStart);

									// Insert this line
									builder.Append($"{delimiter}{{{newName}}}");
									properties![newName] = newLine;
									propertyValues[newName] = newLine;
									delimiter = "\n";

									// Move to the next line
									textLineStart = ++textLineEnd;
									while (textLineEnd < text.Length && text[textLineEnd] != '\n')
									{
										textLineEnd++;
									}
								}

								builder.Append(format, idx + 1, format.Length - (idx + 1));
								format = builder.ToString();
							}
						}
					}
				}

				// Get the enumerable property list for formatting
				propertyValueList = propertyValues.Select(x => new KeyValuePair<string, object?>(x.Key, x.Value));
			}

			// Finally split the format string into multiple lines
			string[] lines = format.Split('\n');
			for (int idx = 0; idx < lines.Length; idx++)
			{
				string message = MessageTemplate.Render(lines[idx], propertyValueList);
				WriteSingleEvent(obj, message, lines[idx], idx, lines.Length, writer);
			}
			return lines.Length;
		}

		static int WriteEventWithMessage(JsonObject obj, string message, IBufferWriter<byte> writer)
		{
			string[] lines = message.Split('\n');
			for (int idx = 0; idx < lines.Length; idx++)
			{
				WriteSingleEvent(obj, lines[idx], null, idx, lines.Length, writer);
			}
			return lines.Length;
		}

		static void WriteSingleEvent(JsonObject obj, string message, string? format, int line, int lineCount, IBufferWriter<byte> writer)
		{
			obj[s_messagePropertyName] = message;
			if (format != null)
			{
				obj[s_formatPropertyName] = format;
			}
			if (lineCount > 1)
			{
				obj[s_linePropertyName] = line;
				obj[s_lineCountPropertyName] = lineCount;
			}

			using (Utf8JsonWriter jsonWriter = new Utf8JsonWriter(writer))
			{
				obj.WriteTo(jsonWriter);
			}

			writer.Write(s_newline.Span);
		}

		void AddEvent(ReadOnlySpan<byte> span, int lineIndex, int lineCount, EventSeverity severity, List<CreateEventRequest> events)
		{
			try
			{
				events.Add(new CreateEventRequest(severity, _logId, lineIndex, lineCount));
			}
			catch (Exception ex)
			{
				_inner.LogError(ex, "Exception while trying to parse line count from data ({Message})", Encoding.UTF8.GetString(span));
			}
		}
	}
}
