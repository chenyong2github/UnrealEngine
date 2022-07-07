// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.ComponentModel;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Unique identifier for a blob, as a utf-8 string. Clients should not assume any internal structure to this identifier; it only
	/// has meaning to the <see cref="IBlobStore"/> implementation.
	/// </summary>
	[JsonConverter(typeof(BlobIdJsonConverter))]
	[TypeConverter(typeof(BlobIdTypeConverter))]
	[CbConverter(typeof(BlobIdCbConverter))]
	public struct BlobId : IEquatable<BlobId>
	{
		/// <summary>
		/// Identifier for the ref
		/// </summary>
		public Utf8String Inner { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="inner"></param>
		public BlobId(Utf8String inner)
		{
			Inner = inner;
		}

		/// <inheritdoc/>
		public override bool Equals(object? obj) => obj is BlobId blobId && Equals(blobId);

		/// <inheritdoc/>
		public override int GetHashCode() => Inner.GetHashCode();

		/// <inheritdoc/>
		public bool Equals(BlobId blobId) => Inner == blobId.Inner;

		/// <inheritdoc/>
		public override string ToString() => Inner.ToString();

		/// <inheritdoc/>
		public static bool operator ==(BlobId lhs, BlobId rhs) => lhs.Equals(rhs);

		/// <inheritdoc/>
		public static bool operator !=(BlobId lhs, BlobId rhs) => !lhs.Equals(rhs);
	}

	/// <summary>
	/// Type converter for BlobId to and from JSON
	/// </summary>
	sealed class BlobIdJsonConverter : JsonConverter<BlobId>
	{
		/// <inheritdoc/>
		public override BlobId Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) => new BlobId(new Utf8String(reader.GetUtf8String().ToArray()));

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter writer, BlobId value, JsonSerializerOptions options) => writer.WriteStringValue(value.Inner.Span);
	}

	/// <summary>
	/// Type converter from strings to BlobId objects
	/// </summary>
	sealed class BlobIdTypeConverter : TypeConverter
	{
		/// <inheritdoc/>
		public override bool CanConvertFrom(ITypeDescriptorContext? context, Type sourceType)
		{
			return sourceType == typeof(string);
		}

		/// <inheritdoc/>
		public override object ConvertFrom(ITypeDescriptorContext? context, CultureInfo? culture, object? value)
		{
			return new BlobId((string)value!);
		}
	}

	/// <summary>
	/// Type converter to compact binary
	/// </summary>
	sealed class BlobIdCbConverter : CbConverterBase<BlobId>
	{
		/// <inheritdoc/>
		public override BlobId Read(CbField field) => new BlobId(field.AsUtf8String());

		/// <inheritdoc/>
		public override void Write(CbWriter writer, BlobId value) => writer.WriteUtf8StringValue(value.Inner);

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter writer, Utf8String name, BlobId value) => writer.WriteUtf8String(name, value.Inner);
	}

	/// <summary>
	/// Extension methods for blob ids
	/// </summary>
	public static class BlobIdExtensions
	{
		/// <summary>
		/// Deserialize a blob id
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		/// <returns>The blob id that was read</returns>
		public static BlobId ReadBlobId(this IMemoryReader reader)
		{
			return new BlobId(reader.ReadUtf8String());
		}

		/// <summary>
		/// Serialize a blob id
		/// </summary>
		/// <param name="writer">Writer to serialize to</param>
		/// <param name="value">Value to serialize</param>
		public static void WriteBlobId(this IMemoryWriter writer, BlobId value)
		{
			writer.WriteUtf8String(value.Inner);
		}
	}
}
