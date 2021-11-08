// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace HordeAgent.Utility
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
		/// <param name="Warnings">Whether to include warnings in the output</param>
		/// <param name="Inner">Additional logger to write to</param>
		public JsonLogger(bool? Warnings, ILogger Inner)
		{
			this.Warnings = Warnings;
			this.Inner = Inner;
		}

		/// <inheritdoc/>
		public IDisposable? BeginScope<TState>(TState State)
		{
			return null;
		}

		/// <inheritdoc/>
		public bool IsEnabled(LogLevel LogLevel)
		{
			return true;
		}

		/// <inheritdoc/>
		public void Log<TState>(LogLevel LogLevel, EventId EventId, TState State, Exception? Exception, Func<TState, Exception?, string> Formatter)
		{
			// Downgrade warnings to information if not required
			if (LogLevel == LogLevel.Warning && !(Warnings ?? true))
			{
				LogLevel = LogLevel.Information;
			}

			if(State is JsonLogEvent JsonEvent)
			{
				WriteFormattedEvent(LogLevel, JsonEvent.Data.ToArray());
				return;
			}

			LogEvent? Event = State as LogEvent;
			if (Event == null)
			{
				Event = LogEvent.FromState(LogLevel, EventId, State, Exception, Formatter);
			}
			WriteFormattedEvent(Event.Level, Event.ToJsonBytes());
		}

		/// <summary>
		/// Writes a formatted event
		/// </summary>
		/// <param name="Level">The log level</param>
		/// <param name="Line">Utf-8 encoded JSON line data</param>
		protected abstract void WriteFormattedEvent(LogLevel Level, byte[] Line);

		/// <summary>
		/// Callback to write a systemic event
		/// </summary>
		/// <param name="EventId">The event id</param>
		/// <param name="Text">The event text</param>
		protected virtual void WriteSystemicEvent(EventId EventId, string Text)
		{
			Inner.LogWarning("Systemic event {KnownLogEventId}: {Text}", EventId.Id, Text);
		}
	}
}
