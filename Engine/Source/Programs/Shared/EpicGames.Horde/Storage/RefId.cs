// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Identifier for a storage namespace
	/// </summary>
	[JsonConverter(typeof(RefIdJsonConverter))]
	[TypeConverter(typeof(RefIdTypeConverter))]
	[CbConverter(typeof(RefIdCbConverter))]
	public struct RefId : IEquatable<RefId>
	{
		/// <summary>
		/// Hash identifier for the ref
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Hash"></param>
		public RefId(IoHash Hash)
		{
			this.Hash = Hash;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name to identify this ref</param>
		public RefId(string Name)
		{
			this.Hash = IoHash.Compute(Encoding.UTF8.GetBytes(Name));
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is RefId RefId && Equals(RefId);

		/// <inheritdoc/>
		public override int GetHashCode() => Hash.GetHashCode();

		/// <inheritdoc/>
		public bool Equals(RefId RefId) => Hash == RefId.Hash;

		/// <inheritdoc/>
		public override string ToString() => Hash.ToString();

		/// <inheritdoc/>
		public static bool operator ==(RefId Lhs, RefId Rhs) => Lhs.Equals(Rhs);

		/// <inheritdoc/>
		public static bool operator !=(RefId Lhs, RefId Rhs) => !Lhs.Equals(Rhs);
	}

	/// <summary>
	/// Type converter for IoHash to and from JSON
	/// </summary>
	sealed class RefIdJsonConverter : JsonConverter<RefId>
	{
		/// <inheritdoc/>
		public override RefId Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options) => new RefId(IoHash.Parse(Reader.ValueSpan));

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, RefId Value, JsonSerializerOptions Options) => Writer.WriteStringValue(Value.Hash.ToUtf8String().Span);
	}

	/// <summary>
	/// Type converter from strings to IoHash objects
	/// </summary>
	sealed class RefIdTypeConverter : TypeConverter
	{
		/// <inheritdoc/>
		public override bool CanConvertFrom(ITypeDescriptorContext Context, Type SourceType)
		{
			return SourceType == typeof(string);
		}

		/// <inheritdoc/>
		public override object ConvertFrom(ITypeDescriptorContext Context, CultureInfo Culture, object Value)
		{
			return new RefId(IoHash.Parse((string)Value));
		}
	}

	/// <summary>
	/// Type converter to compact binary
	/// </summary>
	sealed class RefIdCbConverter : CbConverterBase<RefId>
	{
		/// <inheritdoc/>
		public override RefId Read(CbField Field) => new RefId(Field.AsHash());

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, RefId Value) => Writer.WriteHashValue(Value.Hash);

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, RefId Value) => Writer.WriteHash(Name, Value.Hash);
	}
}
