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
		static Utf8String LevelString { get; } = new Utf8String("Level");
		static Utf8String PropertiesString { get; } = new Utf8String("Properties");
		static Utf8String LineIndexString { get; } = new Utf8String("Line");
		static Utf8String LineCountString { get; } = new Utf8String("LineCount");
		static Utf8String EventIdString { get; } = new Utf8String("EventId");
		static Utf8String IdString { get; } = new Utf8String("Id");
		static Utf8String RenderedMessageString { get; } = new Utf8String("RenderedMessage");

		/// <summary>
		/// The 
		/// </summary>
		public LogLevel Level { get; }

		/// <summary>
		/// The event id, if set
		/// </summary>
		public EventId EventId { get; }

		/// <summary>
		/// Index of this line within the current event
		/// </summary>
		public int LineIndex { get; }

		/// <summary>
		/// Number of lines in the current event
		/// </summary>
		public int LineCount { get; }

		/// <summary>
		/// The utf-8 encoded JSON event
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public JsonLogEvent(LogLevel level, EventId eventId, int lineIndex, int lineCount, ReadOnlyMemory<byte> data)
		{
			Level = level;
			EventId = eventId;
			LineIndex = lineIndex;
			LineCount = lineCount;
			Data = data;
		}

		/// <summary>
		/// Tries to parse a Json log event from the given 
		/// </summary>
		/// <param name="data"></param>
		/// <param name="logEvent"></param>
		/// <returns></returns>
		public static bool TryParse(ReadOnlyMemory<byte> data, out JsonLogEvent logEvent)
		{
			Utf8StringComparer stringComparer = Utf8StringComparer.OrdinalIgnoreCase;
			try
			{
				LogLevel level = LogLevel.None;
				int eventId = 0;
				int lineIndex = 0;
				int lineCount = 1;

				Utf8JsonReader reader = new Utf8JsonReader(data.Span);
				if (reader.Read() && reader.TokenType == JsonTokenType.StartObject)
				{
					ReadOnlySpan<byte> propertyName;
					for (; ReadNextPropertyName(ref reader, out propertyName); reader.Skip())
					{
						if (stringComparer.Equals(propertyName, LevelString.Span))
						{
							if (!Enum.TryParse(reader.GetString(), out level))
							{
								break;
							}
						}
						else if (stringComparer.Equals(propertyName, LineIndexString.Span))
						{
							lineIndex = reader.GetInt32();
						}
						else if (stringComparer.Equals(propertyName, LineCountString.Span))
						{
							lineCount = reader.GetInt32();
						}
						else if (stringComparer.Equals(propertyName, PropertiesString.Span) && reader.TokenType == JsonTokenType.StartObject)
						{
							for (; ReadNextPropertyName(ref reader, out propertyName); reader.Skip())
							{
								if (stringComparer.Equals(EventIdString.Span, propertyName) && reader.TokenType == JsonTokenType.StartObject)
								{
									for (; ReadNextPropertyName(ref reader, out propertyName); reader.Skip())
									{
										if (stringComparer.Equals(IdString.Span, propertyName) && reader.TryGetInt32(out int newEventId))
										{
											eventId = newEventId;
										}
									}
								}
							}
						}
					}
				}

				if (reader.TokenType == JsonTokenType.EndObject && level != LogLevel.None && reader.BytesConsumed == data.Length)
				{
					logEvent = new JsonLogEvent(level, new EventId(eventId), lineIndex, lineCount, data);
					return true;
				}
			}
			catch
			{
			}

			logEvent = default;
			return false;
		}

		static bool ReadNextPropertyName(ref Utf8JsonReader reader, out ReadOnlySpan<byte> propertyName)
		{
			if (reader.Read() && reader.TokenType == JsonTokenType.PropertyName)
			{
				propertyName = reader.ValueSpan;
				return reader.Read();
			}
			else
			{
				propertyName = ReadOnlySpan<byte>.Empty;
				return false;
			}
		}

		/// <summary>
		/// Formats an event as a string
		/// </summary>
		/// <param name="state"></param>
		/// <param name="ex"></param>
		/// <returns></returns>
		/// <exception cref="NotImplementedException"></exception>
		public static string Format(JsonLogEvent state, Exception? ex)
		{
			Utf8JsonReader reader = new Utf8JsonReader(state.Data.Span);
			if (reader.Read() && reader.TokenType == JsonTokenType.StartObject)
			{
				for (; ; )
				{
					if (!reader.Read() || reader.TokenType != JsonTokenType.PropertyName)
					{
						break;
					}

					bool isMessage = Utf8StringComparer.OrdinalIgnoreCase.Equals(reader.ValueSpan, RenderedMessageString.Span);
					if (!reader.TrySkip())
					{
						break;
					}
					if (isMessage && reader.TokenType == JsonTokenType.String)
					{
						return reader.GetString();
					}
				}
			}
			return String.Empty;
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return Encoding.UTF8.GetString(Data.ToArray());
		}
	}
}
