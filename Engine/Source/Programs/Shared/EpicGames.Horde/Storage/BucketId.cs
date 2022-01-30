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
	/// Identifier for a storage bucket
	/// </summary>
	[JsonConverter(typeof(BucketIdJsonConverter))]
	[TypeConverter(typeof(BucketIdTypeConverter))]
	public struct BucketId : IEquatable<BucketId>
	{
		/// <summary>
		/// The text representing this id
		/// </summary>
		readonly StringId Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Input">Unique id for the string</param>
		public BucketId(string Input)
		{
			Inner = new StringId(Input);
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is BucketId Id && Inner.Equals(Id.Inner);

		/// <inheritdoc/>
		public override int GetHashCode() => Inner.GetHashCode();

		/// <inheritdoc/>
		public bool Equals(BucketId Other) => Inner.Equals(Other.Inner);

		/// <inheritdoc/>
		public override string ToString() => Inner.ToString();

		/// <inheritdoc cref="StringId.op_Equality"/>
		public static bool operator ==(BucketId Left, BucketId Right) => Left.Inner == Right.Inner;

		/// <inheritdoc cref="StringId.op_Inequality"/>
		public static bool operator !=(BucketId Left, BucketId Right) => Left.Inner != Right.Inner;
	}

	/// <summary>
	/// Type converter for BucketId to and from JSON
	/// </summary>
	sealed class BucketIdJsonConverter : JsonConverter<BucketId>
	{
		/// <inheritdoc/>
		public override BucketId Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options) => new BucketId(Reader.GetString() ?? String.Empty);

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, BucketId Value, JsonSerializerOptions Options) => Writer.WriteStringValue(Value.ToString());
	}

	/// <summary>
	/// Type converter from strings to BucketId objects
	/// </summary>
	sealed class BucketIdTypeConverter : TypeConverter
	{
		/// <inheritdoc/>
		public override bool CanConvertFrom(ITypeDescriptorContext Context, Type SourceType) => SourceType == typeof(string);

		/// <inheritdoc/>
		public override object ConvertFrom(ITypeDescriptorContext Context, CultureInfo Culture, object Value) => new BucketId((string)Value);
	}
}
