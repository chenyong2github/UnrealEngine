// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Buffers;
using System.Security.Cryptography;
using System.Text;
using System.Buffers.Binary;
using System.Diagnostics.CodeAnalysis;
using System.ComponentModel;
using System.Globalization;
using System.Text.Json.Serialization;
using System.Text.Json;

namespace EpicGames.Core
{
	/// <summary>
	/// Struct representing a strongly typed IoHash value (a 20-byte Blake3 hash).
	/// </summary>
	[JsonConverter(typeof(IoHashJsonConverter))]
	[TypeConverter(typeof(IoHashTypeConverter))]
	public struct IoHash : IEquatable<IoHash>, IComparable<IoHash>
	{
		/// <summary>
		/// Length of an IoHash
		/// </summary>
		public const int NumBytes = 20;

		/// <summary>
		/// Length of the hash in bits
		/// </summary>
		public const int NumBits = NumBytes * 8;

		/// <summary>
		/// Memory storing the digest data
		/// </summary>
		public ReadOnlyMemory<byte> Memory;

		/// <summary>
		/// Span for the underlying memory
		/// </summary>
		public ReadOnlySpan<byte> Span => Memory.Span;

		/// <summary>
		/// Hash consisting of zeroes
		/// </summary>
		public static IoHash Zero { get; } = new IoHash(new byte[NumBytes]);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">Memory to construct from</param>
		public IoHash(ReadOnlyMemory<byte> Memory)
		{
			if (Memory.Length != NumBytes)
			{
				throw new ArgumentException($"IoHash must be {NumBytes} bytes long");
			}

			this.Memory = Memory;
		}

		/// <summary>
		/// Construct 
		/// </summary>
		/// <param name="Hasher">The hasher to construct from</param>
		public IoHash(Blake3.Hasher Hasher)
		{
			byte[] Output = new byte[32];
			Hasher.Finalize(Output);
			Memory = Output.AsMemory(0, NumBytes);
		}

		/// <summary>
		/// Creates a content hash for a block of data, using a given algorithm.
		/// </summary>
		/// <param name="Data">Data to compute the hash for</param>
		/// <returns>New content hash instance containing the hash of the data</returns>
		public static IoHash Compute(ReadOnlySpan<byte> Data)
		{
			byte[] Output = new byte[32];
			Blake3.Hasher.Hash(Data, Output);
			return new IoHash(Output.AsMemory(0, 20));
		}

		/// <summary>
		/// Parses a digest from the given hex string
		/// </summary>
		/// <param name="Text"></param>
		/// <returns></returns>
		public static IoHash Parse(string Text)
		{
			return new IoHash(StringUtils.ParseHexString(Text));
		}

		/// <summary>
		/// Parses a digest from the given hex string
		/// </summary>
		/// <param name="Text"></param>
		/// <returns></returns>
		public static IoHash Parse(ReadOnlySpan<byte> Text)
		{
			return new IoHash(StringUtils.ParseHexString(Text));
		}

		/// <inheritdoc cref="IComparable{T}.CompareTo(T)"/>
		public int CompareTo(IoHash Other)
		{
			ReadOnlySpan<byte> A = Span;
			ReadOnlySpan<byte> B = Other.Span;

			for (int Idx = 0; Idx < A.Length && Idx < B.Length; Idx++)
			{
				int Compare = A[Idx] - B[Idx];
				if (Compare != 0)
				{
					return Compare;
				}
			}
			return A.Length - B.Length;
		}

		/// <inheritdoc/>
		public bool Equals(IoHash Other) => Span.SequenceEqual(Other.Span);

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => (Obj is IoHash Hash) && Hash.Span.SequenceEqual(Span);

		/// <inheritdoc/>
		public override int GetHashCode() => BinaryPrimitives.ReadInt32LittleEndian(Span);

		/// <inheritdoc/>
		public Utf8String ToUtf8String() => StringUtils.FormatUtf8HexString(Memory.Span);

		/// <inheritdoc/>
		public override string ToString() => StringUtils.FormatHexString(Memory.Span);

		/// <summary>
		/// Test two hash values for equality
		/// </summary>
		public static bool operator ==(IoHash A, IoHash B) => A.Span.SequenceEqual(B.Span);

		/// <summary>
		/// Test two hash values for equality
		/// </summary>
		public static bool operator !=(IoHash A, IoHash B) => !(A == B);

		/// <summary>
		/// Tests whether A > B
		/// </summary>
		public static bool operator >(IoHash A, IoHash B) => A.CompareTo(B) > 0;

		/// <summary>
		/// Tests whether A is less than B
		/// </summary>
		public static bool operator <(IoHash A, IoHash B) => A.CompareTo(B) < 0;

		/// <summary>
		/// Tests whether A is greater than or equal to B
		/// </summary>
		public static bool operator >=(IoHash A, IoHash B) => A.CompareTo(B) >= 0;

		/// <summary>
		/// Tests whether A is less than or equal to B
		/// </summary>
		public static bool operator <=(IoHash A, IoHash B) => A.CompareTo(B) <= 0;

		/// <summary>
		/// Convert a Blake3Hash to an IoHash
		/// </summary>
		/// <param name="Hash"></param>
		public static implicit operator IoHash(Blake3Hash Hash)
		{
			return new IoHash(Hash.Memory.Slice(0, NumBytes));
		}
	}

	/// <summary>
	/// Extension methods for dealing with IoHash values
	/// </summary>
	public static class IoHashExtensions
	{
		/// <summary>
		/// Read an <see cref="IoHash"/> from a memory reader
		/// </summary>
		/// <param name="Reader"></param>
		/// <returns></returns>
		public static IoHash ReadIoHash(this MemoryReader Reader)
		{
			return new IoHash(Reader.ReadFixedLengthBytes(IoHash.NumBytes));
		}

		/// <summary>
		/// Write an <see cref="IoHash"/> to a memory writer
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Hash"></param>
		public static void WriteIoHash(this MemoryWriter Writer, IoHash Hash)
		{
			Writer.WriteFixedLengthBytes(Hash.Span);
		}
	}

	/// <summary>
	/// Type converter for IoHash to and from JSON
	/// </summary>
	sealed class IoHashJsonConverter : JsonConverter<IoHash>
	{
		/// <inheritdoc/>
		public override IoHash Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options) => IoHash.Parse(Reader.ValueSpan);

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, IoHash Value, JsonSerializerOptions Options) => Writer.WriteStringValue(Value.ToUtf8String().Span);
	}

	/// <summary>
	/// Type converter from strings to IoHash objects
	/// </summary>
	sealed class IoHashTypeConverter : TypeConverter
	{
		/// <inheritdoc/>
		public override bool CanConvertFrom(ITypeDescriptorContext Context, Type SourceType)
		{
			return SourceType == typeof(string);
		}

		/// <inheritdoc/>
		public override object ConvertFrom(ITypeDescriptorContext Context, CultureInfo Culture, object Value)
		{
			return IoHash.Parse((string)Value);
		}
	}
}
