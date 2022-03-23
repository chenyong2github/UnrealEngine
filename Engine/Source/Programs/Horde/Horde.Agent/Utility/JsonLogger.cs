// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Utility
{
	/// <summary>
	/// Class to handle uploading log data to the server in the background
	/// </summary>
	public abstract class JsonLogger : ILogger
	{
		/// <summary>
		/// Whether the logger should include warnings, or downgrade them to information
		/// </summary>
		protected bool? Warnings { get; }

		/// <summary>
		/// The inner logger device
		/// </summary>
		protected ILogger Inner { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="warnings">Whether to include warnings in the output</param>
		/// <param name="inner">Additional logger to write to</param>
		public JsonLogger(bool? warnings, ILogger inner)
		{
			Warnings = warnings;
			Inner = inner;
		}

		/// <inheritdoc/>
		public IDisposable? BeginScope<TState>(TState state)
		{
			return null;
		}

		/// <inheritdoc/>
		public bool IsEnabled(LogLevel logLevel)
		{
			return true;
		}

		/// <inheritdoc/>
		public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
		{
			// Downgrade warnings to information if not required
			if (logLevel == LogLevel.Warning && !(Warnings ?? true))
			{
				logLevel = LogLevel.Information;
			}

			if(state is JsonLogEvent jsonEvent)
			{
				WriteFormattedEvent(logLevel, jsonEvent.LineIndex, jsonEvent.LineCount, jsonEvent.Data.ToArray());
				return;
			}

			LogEvent? logEvent = state as LogEvent;
			if (logEvent == null)
			{
				logEvent = LogEvent.FromState(logLevel, eventId, state, exception, formatter);
			}
			WriteFormattedEvent(logEvent.Level, logEvent.LineIndex, logEvent.LineCount, logEvent.ToJsonBytes());
		}

		/// <summary>
		/// Writes a formatted event
		/// </summary>
		/// <param name="level">The log level</param>
		/// <param name="lineIndex">Index of the current line within this event</param>
		/// <param name="lineCount">Number of lines in this event</param>
		/// <param name="line">Utf-8 encoded JSON line data</param>
		protected abstract void WriteFormattedEvent(LogLevel level, int lineIndex, int lineCount, byte[] line);

		/// <summary>
		/// Callback to write a systemic event
		/// </summary>
		/// <param name="eventId">The event id</param>
		/// <param name="text">The event text</param>
		protected virtual void WriteSystemicEvent(EventId eventId, string text)
		{
			Inner.LogWarning("Systemic event {KnownLogEventId}: {Text}", eventId.Id, text);
		}
	}
}
