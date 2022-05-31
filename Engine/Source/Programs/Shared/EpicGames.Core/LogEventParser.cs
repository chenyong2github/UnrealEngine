// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Logging;

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

		public LogEventMatch(LogEventPriority priority, LogEvent logEvent)
		{
			Priority = priority;
			Events = new List<LogEvent> { logEvent };
		}

		public LogEventMatch(LogEventPriority priority, IEnumerable<LogEvent> events)
		{
			Priority = priority;
			Events = events.ToList();
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
		/// <param name="cursor">The input buffer</param>
		/// <returns>Information about the error that was matched, or null if an error was not matched</returns>
		LogEventMatch? Match(ILogCursor cursor);
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
		readonly LogBuffer _buffer;

		/// <summary>
		/// Buffer for holding partial line data
		/// </summary>
		readonly MemoryStream _partialLine = new MemoryStream();

		/// <summary>
		/// Whether matching is currently enabled
		/// </summary>
		int _matchingEnabled;

		/// <summary>
		/// The inner logger
		/// </summary>
		ILogger _logger;

		/// <summary>
		/// Public accessor for the logger
		/// </summary>
		public ILogger Logger
		{
			get => _logger;
			set => _logger = value;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="logger">The logger to receive parsed output messages</param>
		public LogEventParser(ILogger logger)
		{
			_logger = logger;
			_buffer = new LogBuffer(50);
		}

		/// <inheritdoc/>
		public void Dispose() => Flush();

		/// <summary>
		/// Enumerate all the types that implement <see cref="ILogEventMatcher"/> in the given assembly, and create instances of them
		/// </summary>
		/// <param name="assembly">The assembly to enumerate matchers from</param>
		public void AddMatchersFromAssembly(Assembly assembly)
		{
			foreach (Type type in assembly.GetTypes())
			{
				if (type.IsClass && typeof(ILogEventMatcher).IsAssignableFrom(type))
				{
					_logger.LogDebug("Adding event matcher: {Type}", type.Name);
					ILogEventMatcher matcher = (ILogEventMatcher)Activator.CreateInstance(type)!;
					Matchers.Add(matcher);
				}
			}
		}

		/// <summary>
		/// Writes a line to the event filter
		/// </summary>
		/// <param name="line">The line to output</param>
		public void WriteLine(string line)
		{
			if (line.Length > 0 && line[0] == '{')
			{
				byte[] data = Encoding.UTF8.GetBytes(line);
				try
				{
					JsonLogEvent jsonEvent;
					if (JsonLogEvent.TryParse(data, out jsonEvent))
					{
						ProcessData(true);
						_logger.Log(jsonEvent.Level, jsonEvent.EventId, jsonEvent, null, JsonLogEvent.Format);
						return;
					}
				}
				catch (Exception ex)
				{
					_logger.LogError(ex, "Exception while parsing log event");
				}
			}

			_buffer.AddLine(StringUtils.ParseEscapeCodes(line));
			ProcessData(false);
		}

		/// <summary>
		/// Writes data to the log parser
		/// </summary>
		/// <param name="data">Data to write</param>
		public void WriteData(ReadOnlyMemory<byte> data)
		{
			int baseIdx = 0;
			int scanIdx = 0;
			ReadOnlySpan<byte> span = data.Span;

			// Handle a partially existing line
			if (_partialLine.Length > 0)
			{
				for (; scanIdx < span.Length; scanIdx++)
				{
					if (span[scanIdx] == '\n')
					{
						_partialLine.Write(span.Slice(baseIdx, scanIdx - baseIdx));
						FlushPartialLine();
						baseIdx = ++scanIdx;
						break;
					}
				}
			}

			// Handle any complete lines
			for (; scanIdx < span.Length; scanIdx++)
			{
				if(span[scanIdx] == '\n')
				{
					AddLine(data.Slice(baseIdx, scanIdx - baseIdx));
					baseIdx = scanIdx + 1;
				}
			}

			// Add the rest of the text to the partial line buffer
			_partialLine.Write(span.Slice(baseIdx));

			// Process the new data
			ProcessData(false);
		}

		/// <summary>
		/// Flushes the current contents of the parser
		/// </summary>
		public void Flush()
		{
			// If there's a partially written line, write that out first
			if (_partialLine.Length > 0)
			{
				FlushPartialLine();
			}

			// Process any remaining data
			ProcessData(true);
		}

		/// <summary>
		/// Adds a raw utf-8 string to the buffer
		/// </summary>
		/// <param name="data">The string data</param>
		private void AddLine(ReadOnlyMemory<byte> data)
		{
			if (data.Length > 0 && data.Span[data.Length - 1] == '\r')
			{
				data = data.Slice(0, data.Length - 1);
			}
			if (data.Length > 0 && data.Span[0] == '{')
			{
				JsonLogEvent jsonEvent;
				if (JsonLogEvent.TryParse(data, out jsonEvent))
				{
					ProcessData(true);
					_logger.Log(jsonEvent.Level, jsonEvent.EventId, jsonEvent, null, JsonLogEvent.Format);
					return;
				}
			}
			_buffer.AddLine(StringUtils.ParseEscapeCodes(Encoding.UTF8.GetString(data.Span)));
		}

		/// <summary>
		/// Writes the current partial line data, with the given data appended to it, then clear the buffer
		/// </summary>
		private void FlushPartialLine()
		{
			AddLine(_partialLine.ToArray());
			_partialLine.Position = 0;
			_partialLine.SetLength(0);
		}

		/// <summary>
		/// Process any data in the buffer
		/// </summary>
		/// <param name="bFlush">Whether we've reached the end of the stream</param>
		void ProcessData(bool bFlush)
		{
			while (_buffer.Length > 0)
			{
				// Try to match an event
				List<LogEvent>? events = null;
				if (Regex.IsMatch(_buffer[0], "<-- Suspend Log Parsing -->", RegexOptions.IgnoreCase))
				{
					_matchingEnabled--;
				}
				else if (Regex.IsMatch(_buffer[0], "<-- Resume Log Parsing -->", RegexOptions.IgnoreCase))
				{
					_matchingEnabled++;
				}
				else if (_matchingEnabled >= 0)
				{
					events = MatchEvent();
				}

				// Bail out if we need more data
				if (_buffer.Length < 1024 && !bFlush && _buffer.NeedMoreData)
				{
					break;
				}

				// If we did match something, check if it's not negated by an ignore pattern. We typically have relatively few errors and many more ignore patterns than matchers, so it's quicker 
				// to check them in response to an identified error than to treat them as matchers of their own.
				if (events != null)
				{
					foreach (Regex ignorePattern in IgnorePatterns)
					{
						if (ignorePattern.IsMatch(_buffer[0]))
						{
							events = null;
							break;
						}
					}
				}

				// Report the error to the listeners
				if (events != null)
				{
					WriteEvents(events);
					_buffer.Advance(events.Count);
				}
				else
				{
					_logger.Log(LogLevel.Information, KnownLogEvents.None, _buffer[0]!, null, (state, exception) => state);
					_buffer.MoveNext();
				}
			}
		}

		/// <summary>
		/// Try to match an event from the current buffer
		/// </summary>
		/// <returns>The matched event</returns>
		private List<LogEvent>? MatchEvent()
		{
			LogEventMatch? currentMatch = null;
			foreach (ILogEventMatcher matcher in Matchers)
			{
				LogEventMatch? match = matcher.Match(_buffer);
				if(match != null)
				{
					if (currentMatch == null || match.Priority > currentMatch.Priority)
					{
						currentMatch = match;
					}
				}
			}
			return currentMatch?.Events;
		}

		/// <summary>
		/// Writes an event to the log
		/// </summary>
		/// <param name="Event">The event to write</param>
		protected virtual void WriteEvents(List<LogEvent> logEvents)
		{
			foreach (LogEvent logEvent in logEvents)
			{
				_logger.Log(logEvent.Level, logEvent.Id, logEvent, null, (state, exception) => state.ToString());
			}
		}
	}
}
