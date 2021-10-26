// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser.Interfaces;
using HordeAgent.Utility;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace HordeAgent.Parser
{
	/// <summary>
	/// Concrete implementation of <see cref="ILogContext"/>
	/// </summary>
	public class LogParserContext : ILogContext
	{
		/// <inheritdoc/>
		public DirectoryReference? WorkspaceDir { get; set; }

		/// <inheritdoc/>
		public string? PerforceStream { get; set; }

		/// <inheritdoc/>
		public int? PerforceChange { get; set; }

		/// <inheritdoc/>
		public bool HasLoggedErrors { get; set; }
	}

	/// <summary>
	/// Turns raw text output into structured logging events
	/// </summary>
	public class LogParser : IDisposable
	{
		/// <summary>
		/// Global list of error matchers, obtained by reflection at startup
		/// </summary>
		static List<ILogEventMatcher> Matchers;

		/// <summary>
		/// List of patterns to ignore
		/// </summary>
		List<string> IgnorePatterns;

		/// <summary>
		/// Buffer of input lines
		/// </summary>
		LineBuffer Buffer;

		/// <summary>
		/// Buffer for holding partial line data
		/// </summary>
		MemoryStream PartialLine = new MemoryStream();

		/// <summary>
		/// The inner logger
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Context for matchers for this log
		/// </summary>
		LogParserContext Context;

		/// <summary>
		/// Refcount for whether output is enabled
		/// </summary>
		int MatchingEnabled;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Logger">The logger to receive parsed output messages</param>
		/// <param name="Context">Context for parsing this log</param>
		/// <param name="IgnorePatterns">List of patterns to ignore</param>
		public LogParser(ILogger Logger, LogParserContext Context, List<string> IgnorePatterns)
		{
			this.IgnorePatterns = IgnorePatterns;
			this.Logger = Logger;
			this.Buffer = new LineBuffer(50);
			this.Context = Context;
		}

		/// <summary>
		/// Static constructor
		/// </summary>
		static LogParser()
		{
			System.Text.RegularExpressions.Regex.CacheSize = 1000;

			Matchers = new List<ILogEventMatcher>();
			foreach (Type Type in Assembly.GetExecutingAssembly().GetTypes())
			{
				if (Type.IsClass && typeof(ILogEventMatcher).IsAssignableFrom(Type))
				{
					ILogEventMatcher Matcher = (ILogEventMatcher)Activator.CreateInstance(Type)!;
					Matchers.Add(Matcher);
				}
			}
		}

		/// <summary>
		/// Adds a raw utf-8 string to the buffer
		/// </summary>
		/// <param name="Data">The string data</param>
		private void AddLine(ReadOnlySpan<byte> Data)
		{
			if(Data.Length > 0 && Data[Data.Length - 1] == '\r')
			{
				Data = Data.Slice(0, Data.Length - 1);
			}
			Buffer.AddLine(Encoding.UTF8.GetString(Data));
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
		/// Writes data to the log parser
		/// </summary>
		/// <param name="Data">Data to write</param>
		/// <param name="bEndOfStream">Whether this is the end of the stream</param>
		public void WriteData(ReadOnlySpan<byte> Data, bool bEndOfStream)
		{
			int BaseIdx = 0;
			int ScanIdx = 0;

			// Handle a partially existing line
			if (PartialLine.Length > 0)
			{
				for (; ScanIdx < Data.Length; ScanIdx++)
				{
					if (Data[ScanIdx] == '\n')
					{
						PartialLine.Write(Data.Slice(BaseIdx, ScanIdx - BaseIdx));
						FlushPartialLine();
						BaseIdx = ++ScanIdx;
						break;
					}
				}
			}

			// Handle any complete lines
			for (; ScanIdx < Data.Length; ScanIdx++)
			{
				if(Data[ScanIdx] == '\n')
				{
					AddLine(Data.Slice(BaseIdx, ScanIdx - BaseIdx));
					BaseIdx = ScanIdx + 1;
				}
			}

			// Add the rest of the text to the partial line buffer
			PartialLine.Write(Data.Slice(BaseIdx));

			// If it's the end of the stream, force a flush
			if (bEndOfStream && PartialLine.Length > 0)
			{
				FlushPartialLine();
			}

			// Process the new data
			ProcessData(bEndOfStream);
		}

		/// <summary>
		/// Writes a line to the event filter
		/// </summary>
		/// <param name="Line">The line to output</param>
		public void WriteLine(string? Line)
		{
			if (Line == null)
			{
				ProcessData(true);
			}
			else
			{
				Buffer.AddLine(Line);
				ProcessData(false);
			}
		}

		/// <summary>
		/// Process any data in the buffer
		/// </summary>
		/// <param name="bEndOfStream">Whether we've reached the end of the stream</param>
		void ProcessData(bool bEndOfStream)
		{
			while (Buffer.Length > 0)
			{
				// Try to match an event
				LogEvent? Event = null;
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
					Event = MatchEvent();
				}

				// Bail out if we need more data
				if(Buffer.Length < 1024 && !bEndOfStream && Buffer.NeedMoreData)
				{
					break;
				}

				// If we did match something, check if it's not negated by an ignore pattern. We typically have relatively few errors and many more ignore patterns than matchers, so it's quicker 
				// to check them in response to an identified error than to treat them as matchers of their own.
				if (Event != null)
				{
					foreach (string IgnorePattern in IgnorePatterns)
					{
						if (Regex.IsMatch(Buffer[0], IgnorePattern))
						{
							Event = null;
							break;
						}
					}
				}

				// Report the error to the listeners
				if (Event != null)
				{
					WriteEvent(Event);
					Buffer.Advance(Event.MaxLineNumber + 1 - Buffer.CurrentLineNumber);
				}
				else
				{
					Logger.Log(LogLevel.Information, KnownLogEvents.None, Buffer[0]!, null, (State, Exception) => State);
					Buffer.MoveNext();
				}

				// Also flag that an error has occurred for future add the error to the log context, so that future errors can examine it
				if (Event != null && Event.Level >= LogLevel.Error)
				{
					Context.HasLoggedErrors = true;
				}
			}
		}

		/// <summary>
		/// Try to match an event from the current buffer
		/// </summary>
		/// <returns>The matched event</returns>
		private LogEvent? MatchEvent()
		{
			LogEvent? Event = null;
			foreach (ILogEventMatcher Matcher in Matchers)
			{
				LogEvent? NewEvent = Matcher.Match(Buffer, Context);
				if (NewEvent != null && (Event == null || NewEvent.Priority > Event.Priority))
				{
					Event = NewEvent;
				}
			}
			return Event;
		}

		/// <summary>
		/// Writes an event to the log
		/// </summary>
		/// <param name="Event">The event to write</param>
		private void WriteEvent(LogEvent Event)
		{
			if (Event.ChildEvents == null)
			{
				Logger.Log(Event.Level, Event.EventId, Event.State, null, (State, Exception) => State.ToString());
			}
			else
			{
				int ChildIdx = 0;
				for (int LineNumber = Buffer.CurrentLineNumber; LineNumber <= Event.MaxLineNumber;)
				{
					if (ChildIdx < Event.ChildEvents.Count && LineNumber == Event.ChildEvents[ChildIdx].MinLineNumber)
					{
						LogEvent ChildEvent = Event.ChildEvents[ChildIdx++];
						Logger.Log(ChildEvent.Level, ChildEvent.EventId, ChildEvent.State, null, (State, Exception) => State.ToString());
						LineNumber = ChildEvent.MaxLineNumber + 1;
					}
					else
					{
						string? Line = Buffer[LineNumber - Buffer.CurrentLineNumber];
						Logger.Log(LogLevel.Information, KnownLogEvents.None, Line!, null, (State, Exception) => State.ToString());
						LineNumber++;
					}
				}
			}
		}

		/// <summary>
		/// Dispose of this object
		/// </summary>
		public void Dispose()
		{
			if(Buffer.Length > 0)
			{
				WriteLine(null);
			}
		}
	}
}
