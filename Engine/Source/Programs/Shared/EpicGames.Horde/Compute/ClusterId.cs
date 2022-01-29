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
	/// Identifier for a compute cluster
	/// </summary>
	[CbConverter(typeof(ClusterIdCbConverter))]
	[JsonConverter(typeof(ClusterIdJsonConverter))]
	[TypeConverter(typeof(ClusterIdTypeConverter))]
	public struct ClusterId : IEquatable<ClusterId>
	{
		/// <summary>
		/// The text representing this id
		/// </summary>
		readonly StringId Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Input">Unique id for the string</param>
		public ClusterId(string Input)
		{
			Inner = new StringId(Input);
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is ClusterId Id && Inner.Equals(Id.Inner);

		/// <inheritdoc/>
		public override int GetHashCode() => Inner.GetHashCode();

		/// <inheritdoc/>
		public bool Equals(ClusterId Other) => Inner.Equals(Other.Inner);

		/// <inheritdoc/>
		public override string ToString() => Inner.ToString();

		/// <inheritdoc cref="StringId.op_Equality"/>
		public static bool operator ==(ClusterId Left, ClusterId Right) => Left.Inner == Right.Inner;

		/// <inheritdoc cref="StringId.op_Inequality"/>
		public static bool operator !=(ClusterId Left, ClusterId Right) => Left.Inner != Right.Inner;
	}

	/// <summary>
	/// Compact binary converter for ClusterId
	/// </summary>
	sealed class ClusterIdCbConverter : CbConverterBase<ClusterId>
	{
		/// <inheritdoc/>
		public override ClusterId Read(CbField Field) => new ClusterId(Field.AsString());

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, ClusterId Value) => Writer.WriteStringValue(Value.ToString());

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, ClusterId Value) => Writer.WriteString(Name, Value.ToString());
	}

	/// <summary>
	/// Type converter for ClusterId to and from JSON
	/// </summary>
	sealed class ClusterIdJsonConverter : JsonConverter<ClusterId>
	{
		/// <inheritdoc/>
		public override ClusterId Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options) => new ClusterId(Reader.GetString() ?? String.Empty);

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, ClusterId Value, JsonSerializerOptions Options) => Writer.WriteStringValue(Value.ToString());
	}

	/// <summary>
	/// Type converter from strings to ClusterId objects
	/// </summary>
	sealed class ClusterIdTypeConverter : TypeConverter
	{
		/// <inheritdoc/>
		public override bool CanConvertFrom(ITypeDescriptorContext Context, Type SourceType) => SourceType == typeof(string);

		/// <inheritdoc/>
		public override object ConvertFrom(ITypeDescriptorContext Context, CultureInfo Culture, object Value) => new ClusterId((string)Value);
	}
}

