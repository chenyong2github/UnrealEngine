// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Parser.Interfaces;
using HordeAgent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace HordeAgent.Parser
{
	/// <summary>
	/// Class to handle uploading log data to the server in the background
	/// </summary>
	class JsonRpcLogger : JsonLogger, IAsyncDisposable
	{
		class QueueItem
		{
			public byte[] Data;
			public CreateEventRequest? Event;

			public QueueItem(byte[] Data, CreateEventRequest? Event)
			{
				this.Data = Data;
				this.Event = Event;
			}

			public override string ToString()
			{
				return Encoding.UTF8.GetString(Data);
			}
		}

		readonly IRpcConnection RpcClient;
		readonly string LogId;
		readonly string? JobId;
		readonly string? JobBatchId;
		readonly string? JobStepId;
		Channel<QueueItem> DataChannel;
		Task? DataWriter;

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
		/// <param name="RpcClient">RPC client to use for server requests</param>
		/// <param name="LogId">The log id to write to</param>
		/// <param name="JobId">Id of the job being executed</param>
		/// <param name="JobBatchId">Batch being executed</param>
		/// <param name="JobStepId">Id of the step being executed</param>
		/// <param name="Warnings">Whether to include warnings in the output</param>
		/// <param name="Inner">Additional logger to write to</param>
		public JsonRpcLogger(IRpcConnection RpcClient, string LogId, string? JobId, string? JobBatchId, string? JobStepId, bool? Warnings, ILogger Inner)
			: base(Warnings, Inner)
		{
			this.RpcClient = RpcClient;
			this.LogId = LogId;
			this.JobId = JobId;
			this.JobBatchId = JobBatchId;
			this.JobStepId = JobStepId;
			this.DataChannel = Channel.CreateUnbounded<QueueItem>();
			this.DataWriter = Task.Run(() => RunDataWriter());
			this.Outcome = JobStepOutcome.Success;
		}

		protected override void WriteFormattedEvent(LogLevel Level, byte[] Line)
		{
			// Update the state of this job if this is an error status
			if (Level == LogLevel.Error || Level == LogLevel.Critical)
			{
				Outcome = JobStepOutcome.Failure;
			}
			else if (Level == LogLevel.Warning && Outcome != JobStepOutcome.Failure)
			{
				Outcome = JobStepOutcome.Warnings;
			}

			// If we want an event for this log event, create one now
			CreateEventRequest? Event = null;
			if (Level == LogLevel.Warning || Level == LogLevel.Error || Level == LogLevel.Critical)
			{
				Event = CreateEvent(Level, 1);
			}

			// Write the data to the output channel
			QueueItem QueueItem = new QueueItem(Line, Event);
			if (!DataChannel.Writer.TryWrite(QueueItem))
			{
				throw new InvalidOperationException("Expected unbounded writer to complete immediately");
			}
		}

		/// <summary>
		/// Callback to write a systemic event
		/// </summary>
		/// <param name="EventId">The event id</param>
		/// <param name="Text">The event text</param>
		protected override void WriteSystemicEvent(EventId EventId, string Text)
		{
			if (JobId == null)
			{
				Inner.LogWarning("Systemic event {KnownLogEventId} in log {LogId}: {Text}", EventId.Id, Text);
			}
			else if (JobBatchId == null)
			{
				Inner.LogWarning("Systemic event {KnownLogEventId} in log {LogId} (job {JobId}): {Text})", EventId.Id, JobId, Text);
			}
			else if (JobStepId == null)
			{
				Inner.LogWarning("Systemic event {KnownLogEventId} in log {LogId} (job batch {JobId}:{BatchId}): {Text})", EventId.Id, JobId, JobStepId, Text);
			}
			else
			{
				Inner.LogWarning("Systemic event {KnownLogEventId} in log {LogId} (job step {JobId}:{BatchId}:{StepId}): {Text}", EventId.Id, LogId, JobId, JobBatchId, JobStepId, Text);
			}
		}

		/// <summary>
		/// Makes a <see cref="CreateEventRequest"/> for the given parameters
		/// </summary>
		/// <param name="LogLevel">Level for this log event</param>
		/// <param name="LineCount">Number of lines in the event</param>
		CreateEventRequest CreateEvent(LogLevel LogLevel, int LineCount)
		{
			EventSeverity Severity = (LogLevel == LogLevel.Warning) ? EventSeverity.Warning : EventSeverity.Error;
			return new CreateEventRequest(Severity, LogId, 0, LineCount);
		}

		/// <summary>
		/// Stops the log writer's background task
		/// </summary>
		/// <returns>Async task</returns>
		public async Task StopAsync()
		{
			if (DataWriter != null)
			{
				DataChannel.Writer.TryComplete();
				await DataWriter;
				DataWriter = null;
			}
		}

		/// <summary>
		/// Dispose of this object. Call StopAsync() to stop asynchronously.
		/// </summary>
		public async ValueTask DisposeAsync()
		{
			await StopAsync();
		}

		/// <summary>
		/// Upload the log data to the server in the background
		/// </summary>
		/// <returns>Async task</returns>
		async Task RunDataWriter()
		{
			// Current position and line number in the log file
			long Offset = 0;
			int LineIndex = 0;

			// Total number of errors and warnings
			const int MaxErrors = 50;
			int NumErrors = 0;
			const int MaxWarnings = 50;
			int NumWarnings = 0;

			// Buffers for chunks and events read in a single iteration
			ArrayBufferWriter<byte> Writer = new ArrayBufferWriter<byte>();
			List<CreateEventRequest> Events = new List<CreateEventRequest>();

			// Line separator for JSON events
			byte[] Newline = { (byte)'\n' };
			JsonEncodedText Timestamp = JsonEncodedText.Encode("");

			// The current jobstep outcome
			JobStepOutcome PostedOutcome = JobStepOutcome.Success;

			// Whether we've written the flush command
			for (; ; )
			{
				Writer.Clear();
				Events.Clear();

				// Save off the current line number for sending to the server
				int InitialLineIndex = LineIndex;

				// Get the next data
				Task WaitTask = Task.Delay(TimeSpan.FromSeconds(2.0));
				while (Writer.WrittenCount < 256 * 1024)
				{
					QueueItem? Data;
					if (DataChannel.Reader.TryRead(out Data))
					{
						if (Data.Event != null)
						{
							if ((Data.Event.Severity == EventSeverity.Warning && ++NumWarnings < MaxWarnings) || (Data.Event.Severity == EventSeverity.Error && ++NumErrors < MaxErrors))
							{
								Data.Event.LineIndex = LineIndex;
								Events.Add(Data.Event);
							}
						}

						Writer.Write(Data.Data);
						Writer.Write(Newline);

						LineIndex++;
					}
					else
					{
						Task<bool> ReadTask = DataChannel.Reader.WaitToReadAsync().AsTask();
						if (await Task.WhenAny(ReadTask, WaitTask) == WaitTask)
						{
							break;
						}
						if (!await ReadTask)
						{
							break;
						}
					}
				}

				// Upload it to the server
				if (Writer.WrittenCount > 0)
				{
					byte[] Data = Writer.WrittenSpan.ToArray();
					try
					{
						await RpcClient.InvokeAsync(x => x.WriteOutputAsync(new WriteOutputRequest(LogId, Offset, InitialLineIndex, Data, false)), new RpcContext(), CancellationToken.None);
					}
					catch (Exception Ex)
					{
						Inner.LogWarning(Ex, "Unable to write data to server (log {LogId}, offset {Offset})", LogId, Offset);
					}
					Offset += Data.Length;
				}

				// Write all the events
				if (Events.Count > 0)
				{
					try
					{
						await RpcClient.InvokeAsync(x => x.CreateEventsAsync(new CreateEventsRequest(Events)), new RpcContext(), CancellationToken.None);
					}
					catch (Exception Ex)
					{
						Inner.LogWarning(Ex, "Unable to create events");
					}
				}

				// Update the outcome of this jobstep
				if (JobId != null && JobBatchId != null && JobStepId != null && Outcome != PostedOutcome)
				{
					try
					{
						await RpcClient.InvokeAsync(x => x.UpdateStepAsync(new UpdateStepRequest(JobId, JobBatchId, JobStepId, JobStepState.Unspecified, Outcome)), new RpcContext(), CancellationToken.None);
					}
					catch (Exception Ex)
					{
						Inner.LogWarning(Ex, "Unable to update step outcome to {NewOutcome}", Outcome);
					}
					PostedOutcome = Outcome;
				}

				// Wait for more data to be available
				if (!await DataChannel.Reader.WaitToReadAsync())
				{
					try
					{
						await RpcClient.InvokeAsync(x => x.WriteOutputAsync(new WriteOutputRequest(LogId, Offset, LineIndex, Array.Empty<byte>(), true)), new RpcContext(), CancellationToken.None);
					}
					catch (Exception Ex)
					{
						Inner.LogWarning(Ex, "Unable to flush data to server (log {LogId}, offset {Offset})", LogId, Offset);
					}
					break;
				}
			}
		}
	}
}
