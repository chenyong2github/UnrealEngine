// Copyright Epic Games, Inc. All Rights Reserved.

using System.ComponentModel;
using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Serialization;
using Horde.Server.Utilities;
using OpenTracing;

namespace Horde.Server.Streams
{
	/// <summary>
	/// Identifier for a stream
	/// </summary>
	/// <param name="Id">Id to construct from</param>
	[JsonSchemaString]
	[TypeConverter(typeof(StringIdTypeConverter<StreamId, StreamIdConverter>))]
	[StringIdConverter(typeof(StreamIdConverter))]
	[CbConverter(typeof(StringIdCbConverter<StreamId, StreamIdConverter>))]
	public record struct StreamId(StringId Id)
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public StreamId(string id) : this(new StringId(id))
		{
		}

		/// <inheritdoc cref="StringId.IsEmpty"/>
		public bool IsEmpty => Id.IsEmpty;

		/// <inheritdoc/>
		public override string ToString() => Id.ToString();
	}

	/// <summary>
	/// Converter to and from <see cref="StringId"/> instances.
	/// </summary>
	class StreamIdConverter : StringIdConverter<StreamId>
	{
		/// <inheritdoc/>
		public override StreamId FromStringId(StringId id) => new StreamId(id);

		/// <inheritdoc/>
		public override StringId ToStringId(StreamId value) => value.Id;
	}

	/// <summary>
	/// Extension methods for stream id values
	/// </summary>
	public static class StreamIdExtensions
	{
		/// <inheritdoc cref="ISpan.SetTag(System.String, System.String)"/>
		public static ISpan SetTag(this ISpan span, string key, StreamId value) => span.SetTag(key, value.Id.ToString());

		/// <inheritdoc cref="ISpan.SetTag(System.String, System.String)"/>
		public static ISpan SetTag(this ISpan span, string key, StreamId? value) => span.SetTag(key, value.HasValue ? value.Value.ToString() : null);
	}
}
