// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Identifier for a storage namespace
	/// </summary>
	[JsonConverter(typeof(NamespaceIdJsonConverter))]
	[TypeConverter(typeof(NamespaceIdTypeConverter))]
	public struct NamespaceId : IEquatable<NamespaceId>
	{
		/// <summary>
		/// The text representing this id
		/// </summary>
		readonly StringId Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Input">Unique id for the string</param>
		public NamespaceId(string Input)
		{
			Inner = new StringId(Input);
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is NamespaceId Id && Inner.Equals(Id.Inner);

		/// <inheritdoc/>
		public override int GetHashCode() => Inner.GetHashCode();

		/// <inheritdoc/>
		public bool Equals(NamespaceId Other) => Inner.Equals(Other.Inner);

		/// <inheritdoc/>
		public override string ToString() => Inner.ToString();

		/// <inheritdoc cref="StringId.op_Equality"/>
		public static bool operator ==(NamespaceId Left, NamespaceId Right) => Left.Inner == Right.Inner;

		/// <inheritdoc cref="StringId.op_Inequality"/>
		public static bool operator !=(NamespaceId Left, NamespaceId Right) => Left.Inner != Right.Inner;
	}

	/// <summary>
	/// Type converter for NamespaceId to and from JSON
	/// </summary>
	sealed class NamespaceIdJsonConverter : JsonConverter<NamespaceId>
	{
		/// <inheritdoc/>
		public override NamespaceId Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options) => new NamespaceId(Reader.GetString() ?? String.Empty);

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, NamespaceId Value, JsonSerializerOptions Options) => Writer.WriteStringValue(Value.ToString());
	}

	/// <summary>
	/// Type converter from strings to NamespaceId objects
	/// </summary>
	sealed class NamespaceIdTypeConverter : TypeConverter
	{
		/// <inheritdoc/>
		public override bool CanConvertFrom(ITypeDescriptorContext Context, Type SourceType) => SourceType == typeof(string);

		/// <inheritdoc/>
		public override object ConvertFrom(ITypeDescriptorContext Context, CultureInfo Culture, object Value) => new NamespaceId((string)Value);
	}
}
