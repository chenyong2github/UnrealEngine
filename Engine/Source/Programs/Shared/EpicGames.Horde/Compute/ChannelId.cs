// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Identifier for a channel for receiving compute responses
	/// </summary>
	[CbConverter(typeof(ChannelIdCbConverter))]
	[JsonConverter(typeof(ChannelIdJsonConverter))]
	[TypeConverter(typeof(ChannelIdTypeConverter))]
	public struct ChannelId : IEquatable<ChannelId>
	{
		/// <summary>
		/// The text representing this id
		/// </summary>
		readonly StringId Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Input">Unique id for the string</param>
		public ChannelId(string Input)
		{
			Inner = new StringId(Input);
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is ChannelId Id && Inner.Equals(Id.Inner);

		/// <inheritdoc/>
		public override int GetHashCode() => Inner.GetHashCode();

		/// <inheritdoc/>
		public bool Equals(ChannelId Other) => Inner.Equals(Other.Inner);

		/// <inheritdoc/>
		public override string ToString() => Inner.ToString();

		/// <inheritdoc cref="StringId.op_Equality"/>
		public static bool operator ==(ChannelId Left, ChannelId Right) => Left.Inner == Right.Inner;

		/// <inheritdoc cref="StringId.op_Inequality"/>
		public static bool operator !=(ChannelId Left, ChannelId Right) => Left.Inner != Right.Inner;
	}

	/// <summary>
	/// Compact binary converter for ChannelId
	/// </summary>
	sealed class ChannelIdCbConverter : CbConverterBase<ChannelId>
	{
		/// <inheritdoc/>
		public override ChannelId Read(CbField Field) => new ChannelId(Field.AsString());

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, ChannelId Value) => Writer.WriteStringValue(Value.ToString());

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, ChannelId Value) => Writer.WriteString(Name, Value.ToString());
	}

	/// <summary>
	/// Type converter for ChannelId to and from JSON
	/// </summary>
	sealed class ChannelIdJsonConverter : JsonConverter<ChannelId>
	{
		/// <inheritdoc/>
		public override ChannelId Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options) => new ChannelId(Reader.GetString() ?? String.Empty);

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, ChannelId Value, JsonSerializerOptions Options) => Writer.WriteStringValue(Value.ToString());
	}

	/// <summary>
	/// Type converter from strings to ChannelId objects
	/// </summary>
	sealed class ChannelIdTypeConverter : TypeConverter
	{
		/// <inheritdoc/>
		public override bool CanConvertFrom(ITypeDescriptorContext Context, Type SourceType) => SourceType == typeof(string);

		/// <inheritdoc/>
		public override object ConvertFrom(ITypeDescriptorContext Context, CultureInfo Culture, object Value) => new ChannelId((string)Value);
	}
}
