// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging;

namespace EpicGames.Core
{
	/// <summary>
	/// Defines a preformatted Json log event, which can pass through raw Json data directly or format it as a regular string
	/// </summary>
	public struct JsonLogEvent
	{
		/// <summary>
		/// The log level
		/// </summary>
		public LogLevel Level { get; }

		/// <summary>
		/// The event id, if set
		/// </summary>
		public EventId EventId { get; }

		/// <summary>
		/// The utf-8 encoded JSON event
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public JsonLogEvent(LogLevel level, EventId eventId, ReadOnlyMemory<byte> data)
		{
			Level = level;
			EventId = eventId;
			Data = data;
		}

		/// <summary>
		/// Creates a json log event from the given logger paramters
		/// </summary>
		/// <inheritdoc cref="ILogger.Log{TState}(LogLevel, EventId, TState, Exception, Func{TState, Exception, String})"/>
		public static JsonLogEvent FromLoggerState<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
		{
			if (state is JsonLogEvent jsonLogEvent)
			{
				return jsonLogEvent;
			}

			LogEvent? logEvent = state as LogEvent;
			if (logEvent == null)
			{
				logEvent = LogEvent.FromState(logLevel, eventId, state, exception, formatter);
			}

			return new JsonLogEvent(logLevel, eventId, logEvent.ToJsonBytes());
		}

		/// <summary>
		/// Tries to parse a Json log event from the given 
		/// </summary>
		/// <param name="data"></param>
		/// <param name="logEvent"></param>
		/// <returns></returns>
		public static bool TryParse(ReadOnlyMemory<byte> data, out JsonLogEvent logEvent)
		{
			try
			{
				LogLevel level = LogLevel.None;
				int eventId = 0;

				Utf8JsonReader reader = new Utf8JsonReader(data.Span);
				if (reader.Read() && reader.TokenType == JsonTokenType.StartObject)
				{
					while (reader.Read() && reader.TokenType == JsonTokenType.PropertyName)
					{
						ReadOnlySpan<byte> propertyName = reader.ValueSpan;
						if (!reader.Read())
						{
							break;
						}
						else if (propertyName.SequenceEqual(LogEventPropertyName.Level) && reader.TokenType == JsonTokenType.String)
						{
							level = ParseLevel(reader.ValueSpan);
						}
						else if (propertyName.SequenceEqual(LogEventPropertyName.Id) && reader.TokenType == JsonTokenType.Number)
						{
							eventId = reader.GetInt32();
						}
						reader.Skip();
					}
				}

				if (reader.TokenType == JsonTokenType.EndObject && level != LogLevel.None && reader.BytesConsumed == data.Length)
				{
					logEvent = new JsonLogEvent(level, new EventId(eventId), data.ToArray());
					return true;
				}
			}
			catch
			{
			}

			logEvent = default;
			return false;
		}

		static readonly sbyte[] s_firstCharToLogLevel;
		static readonly byte[][] s_logLevelNames;

		static JsonLogEvent()
		{
			const int LogLevelCount = (int)LogLevel.None;

			s_firstCharToLogLevel = new sbyte[256];
			Array.Fill(s_firstCharToLogLevel, (sbyte)-1);

			s_logLevelNames = new byte[LogLevelCount][];
			for (int idx = 0; idx < (int)LogLevel.None; idx++)
			{
				byte[] name = Encoding.UTF8.GetBytes(Enum.GetName(typeof(LogLevel), (LogLevel)idx)!);
				s_logLevelNames[idx] = name;
				s_firstCharToLogLevel[name[0]] = (sbyte)idx;
			}
		}

		static LogLevel ParseLevel(ReadOnlySpan<byte> level)
		{
			int result = s_firstCharToLogLevel[level[0]];
			if (!level.SequenceEqual(s_logLevelNames[result]))
			{
				throw new InvalidOperationException();
			}
			return (LogLevel)result;
		}

		/// <summary>
		/// Formats an event as a string
		/// </summary>
		/// <param name="state"></param>
		/// <returns></returns>
		/// <exception cref="NotImplementedException"></exception>
		public static string Format(JsonLogEvent state, Exception? _) => LogEvent.Read(state.Data.Span).ToString();

		/// <inheritdoc/>
		public override string ToString() => Encoding.UTF8.GetString(Data.ToArray());
	}
}
