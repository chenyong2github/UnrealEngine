// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;

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
		public JsonLogEvent(LogLevel Level, EventId EventId, int LineIndex, int LineCount, ReadOnlyMemory<byte> Data)
		{
			this.Level = Level;
			this.EventId = EventId;
			this.LineIndex = LineIndex;
			this.LineCount = LineCount;
			this.Data = Data;
		}

		/// <summary>
		/// Tries to parse a Json log event from the given 
		/// </summary>
		/// <param name="Data"></param>
		/// <param name="Event"></param>
		/// <returns></returns>
		public static bool TryParse(ReadOnlyMemory<byte> Data, out JsonLogEvent Event)
		{
			Utf8StringComparer StringComparer = Utf8StringComparer.OrdinalIgnoreCase;
			try
			{
				LogLevel Level = LogLevel.None;
				int EventId = 0;
				int LineIndex = 0;
				int LineCount = 1;

				Utf8JsonReader Reader = new Utf8JsonReader(Data.Span);
				if (Reader.Read() && Reader.TokenType == JsonTokenType.StartObject)
				{
					ReadOnlySpan<byte> PropertyName;
					for (; ReadNextPropertyName(ref Reader, out PropertyName); Reader.Skip())
					{
						if (StringComparer.Equals(PropertyName, LevelString.Span))
						{
							if (!Enum.TryParse(Reader.GetString(), out Level))
							{
								break;
							}
						}
						else if (StringComparer.Equals(PropertyName, LineIndexString.Span))
						{
							LineIndex = Reader.GetInt32();
						}
						else if (StringComparer.Equals(PropertyName, LineCountString.Span))
						{
							LineCount = Reader.GetInt32();
						}
						else if (StringComparer.Equals(PropertyName, PropertiesString.Span) && Reader.TokenType == JsonTokenType.StartObject)
						{
							for (; ReadNextPropertyName(ref Reader, out PropertyName); Reader.Skip())
							{
								if (StringComparer.Equals(EventIdString.Span, PropertyName) && Reader.TokenType == JsonTokenType.StartObject)
								{
									for (; ReadNextPropertyName(ref Reader, out PropertyName); Reader.Skip())
									{
										if (StringComparer.Equals(IdString.Span, PropertyName) && Reader.TryGetInt32(out int NewEventId))
										{
											EventId = NewEventId;
										}
									}
								}
							}
						}
					}
				}

				if (Reader.TokenType == JsonTokenType.EndObject && Level != LogLevel.None && Reader.BytesConsumed == Data.Length)
				{
					Event = new JsonLogEvent(Level, new EventId(EventId), LineIndex, LineCount, Data);
					return true;
				}
			}
			catch
			{
			}

			Event = default;
			return false;
		}

		static bool ReadNextPropertyName(ref Utf8JsonReader Reader, out ReadOnlySpan<byte> PropertyName)
		{
			if (Reader.Read() && Reader.TokenType == JsonTokenType.PropertyName)
			{
				PropertyName = Reader.ValueSpan;
				return Reader.Read();
			}
			else
			{
				PropertyName = ReadOnlySpan<byte>.Empty;
				return false;
			}
		}

		/// <summary>
		/// Formats an event as a string
		/// </summary>
		/// <param name="State"></param>
		/// <param name="Ex"></param>
		/// <returns></returns>
		/// <exception cref="NotImplementedException"></exception>
		public static string Format(JsonLogEvent State, Exception? Ex)
		{
			Utf8JsonReader Reader = new Utf8JsonReader(State.Data.Span);
			if (Reader.Read() && Reader.TokenType == JsonTokenType.StartObject)
			{
				for (; ; )
				{
					if (!Reader.Read() || Reader.TokenType != JsonTokenType.PropertyName)
					{
						break;
					}

					bool IsMessage = Utf8StringComparer.OrdinalIgnoreCase.Equals(Reader.ValueSpan, RenderedMessageString.Span);
					if (!Reader.TrySkip())
					{
						break;
					}
					if (IsMessage && Reader.TokenType == JsonTokenType.String)
					{
						return Reader.GetString();
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
