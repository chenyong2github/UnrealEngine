// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.ComponentModel;
using System.Linq;
using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Serialization;
using Horde.Server.Utilities;
using OpenTracing;

namespace Horde.Server.Streams
{
	/// <summary>
	/// Identifier for a job template
	/// </summary>
	/// <param name="Id">Id to construct from</param>
	[JsonSchemaString]
	[TypeConverter(typeof(StringIdTypeConverter<TemplateId, TemplateIdConverter>))]
	[StringIdConverter(typeof(TemplateIdConverter))]
	[CbConverter(typeof(StringIdCbConverter<TemplateId, TemplateIdConverter>))]
	public record struct TemplateId(StringId Id)
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public TemplateId(string id) : this(new StringId(id))
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
	class TemplateIdConverter : StringIdConverter<TemplateId>
	{
		/// <inheritdoc/>
		public override TemplateId FromStringId(StringId id) => new TemplateId(id);

		/// <inheritdoc/>
		public override StringId ToStringId(TemplateId value) => value.Id;
	}

	/// <summary>
	/// Extension methods for stream id values
	/// </summary>
	public static class TemplateIdExtensions
	{
		/// <inheritdoc cref="ISpan.SetTag(System.String, System.String)"/>
		public static ISpan SetTag(this ISpan span, string key, TemplateId value) => span.SetTag(key, value.Id.ToString());

		/// <inheritdoc cref="ISpan.SetTag(System.String, System.String)"/>
		public static ISpan SetTag(this ISpan span, string key, TemplateId[]? values) => span.SetTag(key, (values != null)? String.Join(',', values.Select(x => x.Id.ToString())) : null);
	}
}
