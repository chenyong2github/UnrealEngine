// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Buffers.Text;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Confidence of a matched log event being the correct derivation
	/// </summary>
	public enum LogEventPriority
	{
		None,
		Lowest,
		Low,
		BelowNormal,
		Normal,
		AboveNormal,
		High,
		Highest,
	}

	public class LogEventMatch
	{
		public LogEventPriority Priority { get; }
		public List<LogEvent> Events { get; }

		public LogEventMatch(LogEventPriority Priority, LogEvent Event)
		{
			this.Priority = Priority;
			this.Events = new List<LogEvent> { Event };
		}

		public LogEventMatch(LogEventPriority Priority, IEnumerable<LogEvent> Events)
		{
			this.Priority = Priority;
			this.Events = Events.ToList();
		}
	}

	/// <summary>
	/// Interface for a class which matches error strings
	/// </summary>
	public interface ILogEventMatcher
	{
		/// <summary>
		/// Attempt to match events from the given input buffer
		/// </summary>
		/// <param name="Cursor">The input buffer</param>
		/// <returns>Information about the error that was matched, or null if an error was not matched</returns>
		LogEventMatch? Match(ILogCursor Cursor);
	}

	/// <summary>
	/// Turns raw text output into structured logging events
	/// </summary>
	public class LogEventParser : IDisposable
	{
		/// <summary>
		/// List of event matchers for this parser
		/// </summary>
		public List<ILogEventMatcher> Matchers { get; } = new List<ILogEventMatcher>();

		/// <summary>
		/// List of patterns to ignore
		/// </summary>
		public List<Regex> IgnorePatterns { get; } = new List<Regex>();

		/// <summary>
		/// Buffer of input lines
		/// </summary>
		LogBuffer Buffer;

		/// <summary>
		/// Buffer for holding partial line data
		/// </summary>
		MemoryStream PartialLine = new MemoryStream();

		/// <summary>
		/// Whether matching is currently enabled
		/// </summary>
		int MatchingEnabled;

		/// <summary>
		/// The inner logger
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Logger">The logger to receive parsed output messages</param>
		public LogEventParser(ILogger Logger)
		{
			this.Logger = Logger;
			this.Buffer = new LogBuffer(50);
		}

		/// <inheritdoc/>
		public void Dispose() => Flush();

		/// <summary>
		/// Writes a line to the event filter
		/// </summary>
		/// <param name="Line">The line to output</param>
		public void WriteLine(string Line)
		{
			Buffer.AddLine(Line);
			ProcessData(false);
		}

		/// <summary>
		/// Writes data to the log parser
		/// </summary>
		/// <param name="Data">Data to write</param>
		public void WriteData(ReadOnlyMemory<byte> Data)
		{
			int BaseIdx = 0;
			int ScanIdx = 0;
			ReadOnlySpan<byte> Span = Data.Span;

			// Handle a partially existing line
			if (PartialLine.Length > 0)
			{
				for (; ScanIdx < Span.Length; ScanIdx++)
				{
					if (Span[ScanIdx] == '\n')
					{
						PartialLine.Write(Span.Slice(BaseIdx, ScanIdx - BaseIdx));
						FlushPartialLine();
						BaseIdx = ++ScanIdx;
						break;
					}
				}
			}

			// Handle any complete lines
			for (; ScanIdx < Span.Length; ScanIdx++)
			{
				if(Span[ScanIdx] == '\n')
				{
					AddLine(Data.Slice(BaseIdx, ScanIdx - BaseIdx));
					BaseIdx = ScanIdx + 1;
				}
			}

			// Add the rest of the text to the partial line buffer
			PartialLine.Write(Span.Slice(BaseIdx));

			// Process the new data
			ProcessData(false);
		}

		/// <summary>
		/// Flushes the current contents of the parser
		/// </summary>
		public void Flush()
		{
			// If there's a partially written line, write that out first
			if (PartialLine.Length > 0)
			{
				FlushPartialLine();
			}

			// Process any remaining data
			ProcessData(true);
		}

		/// <summary>
		/// Adds a raw utf-8 string to the buffer
		/// </summary>
		/// <param name="Data">The string data</param>
		private void AddLine(ReadOnlyMemory<byte> Data)
		{
			if (Data.Length > 0 && Data.Span[Data.Length - 1] == '\r')
			{
				Data = Data.Slice(0, Data.Length - 1);
			}
			if (Data.Length > 0 && Data.Span[0] == '{')
			{
				JsonLogEvent JsonEvent;
				if (JsonLogEvent.TryParse(Data, out JsonEvent))
				{
					ProcessData(true);
					Logger.Log(JsonEvent.Level, JsonEvent.EventId, JsonEvent, null, JsonLogEvent.Format);
					return;
				}
			}
			Buffer.AddLine(Encoding.UTF8.GetString(Data.Span));
		}

		/// <summary>
		/// Writes the current partial line data, with the given data appended to it, then clear the buffer
		/// </summary>
		private void FlushPartialLine()
		{
			AddLine(PartialLine.ToArray());
			PartialLine.Position = 0;
			PartialLine.SetLength(0);
		}

		/// <summary>
		/// Process any data in the buffer
		/// </summary>
		/// <param name="bFlush">Whether we've reached the end of the stream</param>
		void ProcessData(bool bFlush)
		{
			while (Buffer.Length > 0)
			{
				// Try to match an event
				List<LogEvent>? Events = null;
				if (Regex.IsMatch(Buffer[0], "<-- Suspend Log Parsing -->", RegexOptions.IgnoreCase))
				{
					MatchingEnabled--;
				}
				else if (Regex.IsMatch(Buffer[0], "<-- Resume Log Parsing -->", RegexOptions.IgnoreCase))
				{
					MatchingEnabled++;
				}
				else if (MatchingEnabled >= 0)
				{
					Events = MatchEvent();
				}

				// Bail out if we need more data
				if (Buffer.Length < 1024 && !bFlush && Buffer.NeedMoreData)
				{
					break;
				}

				// If we did match something, check if it's not negated by an ignore pattern. We typically have relatively few errors and many more ignore patterns than matchers, so it's quicker 
				// to check them in response to an identified error than to treat them as matchers of their own.
				if (Events != null)
				{
					foreach (Regex IgnorePattern in IgnorePatterns)
					{
						if (IgnorePattern.IsMatch(Buffer[0]))
						{
							Events = null;
							break;
						}
					}
				}

				// Report the error to the listeners
				if (Events != null)
				{
					WriteEvents(Events);
					Buffer.Advance(Events.Count);
				}
				else
				{
					Logger.Log(LogLevel.Information, KnownLogEvents.None, Buffer[0]!, null, (State, Exception) => State);
					Buffer.MoveNext();
				}
			}
		}

		/// <summary>
		/// Try to match an event from the current buffer
		/// </summary>
		/// <returns>The matched event</returns>
		private List<LogEvent>? MatchEvent()
		{
			LogEventMatch? CurrentMatch = null;
			foreach (ILogEventMatcher Matcher in Matchers)
			{
				LogEventMatch? Match = Matcher.Match(Buffer);
				if(Match != null)
				{
					if (CurrentMatch == null || Match.Priority > CurrentMatch.Priority)
					{
						CurrentMatch = Match;
					}
				}
			}
			return CurrentMatch?.Events;
		}

		/// <summary>
		/// Writes an event to the log
		/// </summary>
		/// <param name="Event">The event to write</param>
		protected virtual void WriteEvents(List<LogEvent> Events)
		{
			foreach (LogEvent Event in Events)
			{
				Logger.Log(Event.Level, Event.Id, Event, null, (State, Exception) => State.ToString());
			}
		}
	}
}
