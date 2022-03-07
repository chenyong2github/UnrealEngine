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

		ulong A;
		ulong B;
		uint C;

		/// <summary>
		/// Hash consisting of zeroes
		/// </summary>
		public static IoHash Zero { get; } = new IoHash(0, 0, 0);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">Memory to construct from</param>
		public IoHash(ReadOnlySpan<byte> Span)
			: this(BinaryPrimitives.ReadUInt64BigEndian(Span), BinaryPrimitives.ReadUInt64BigEndian(Span.Slice(8)), BinaryPrimitives.ReadUInt32BigEndian(Span.Slice(16)))
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">Memory to construct from</param>
		public IoHash(ulong A, ulong B, uint C)
		{
			this.A = A;
			this.B = B;
			this.C = C;
		}

		/// <summary>
		/// Construct 
		/// </summary>
		/// <param name="Hasher">The hasher to construct from</param>
		public static IoHash FromBlake3(Blake3.Hasher Hasher)
		{
			byte[] Output = new byte[32];
			Hasher.Finalize(Output);
			return new IoHash(Output);
		}

		/// <summary>
		/// Creates the IoHash for a block of data.
		/// </summary>
		/// <param name="Data">Data to compute the hash for</param>
		/// <returns>New hash instance containing the hash of the data</returns>
		public static IoHash Compute(ReadOnlySpan<byte> Data)
		{
			byte[] Output = new byte[32];
			Blake3.Hasher.Hash(Data, Output);
			return new IoHash(Output);
		}

		/// <summary>
		/// Creates the IoHash for a block of data.
		/// </summary>
		/// <param name="Sequence">Data to compute the hash for</param>
		/// <returns>New hash instance containing the hash of the data</returns>
		public static IoHash Compute(ReadOnlySequence<byte> Sequence)
		{
			if (Sequence.IsSingleSegment)
			{
				return Compute(Sequence.FirstSpan);
			}

			using (Blake3.Hasher Hasher = Blake3.Hasher.New())
			{
				foreach (ReadOnlyMemory<byte> Segment in Sequence)
				{
					Hasher.Update(Segment.Span);
				}
				return FromBlake3(Hasher);
			}
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

		/// <summary>
		/// Parses a digest from the given hex string
		/// </summary>
		/// <param name="Text"></param>
		/// <param name="Hash">Receives the hash on success</param>
		/// <returns></returns>
		public static bool TryParse(string Text, out IoHash Hash)
		{
			byte[]? Bytes;
			if (StringUtils.TryParseHexString(Text, out Bytes) && Bytes.Length == IoHash.NumBytes)
			{
				Hash = new IoHash(Bytes);
				return true;
			}
			else
			{
				Hash = default;
				return false;
			}
		}

		/// <summary>
		/// Parses a digest from the given hex string
		/// </summary>
		/// <param name="Text"></param>
		/// <param name="Hash">Receives the hash on success</param>
		/// <returns></returns>
		public static bool TryParse(ReadOnlySpan<byte> Text, out IoHash Hash)
		{
			byte[]? Bytes;
			if (StringUtils.TryParseHexString(Text, out Bytes) && Bytes.Length == IoHash.NumBytes)
			{
				Hash = new IoHash(Bytes);
				return true;
			}
			else
			{
				Hash = default;
				return false;
			}
		}

		/// <inheritdoc cref="IComparable{T}.CompareTo(T)"/>
		public int CompareTo(IoHash Other)
		{
			if (A != Other.A)
			{
				return (A < Other.A) ? -1 : +1;
			}
			else if (B != Other.B)
			{
				return (B < Other.B) ? -1 : +1;
			}
			else
			{
				return (C < Other.C) ? -1 : +1;
			}
		}

		/// <inheritdoc/>
		public bool Equals(IoHash Other) => A == Other.A && B == Other.B && C == Other.C;

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => (Obj is IoHash Hash) && Equals(Hash);

		/// <inheritdoc/>
		public override int GetHashCode() => (int)A;

		/// <inheritdoc/>
		public Utf8String ToUtf8String() => StringUtils.FormatUtf8HexString(ToByteArray());

		/// <inheritdoc/>
		public override string ToString() => StringUtils.FormatHexString(ToByteArray());

		public byte[] ToByteArray()
		{
			byte[] Data = new byte[NumBytes];
			CopyTo(Data);
			return Data;
		}

		/// <summary>
		/// Copies this hash into a span
		/// </summary>
		/// <param name="Span"></param>
		public void CopyTo(Span<byte> Span)
		{
			BinaryPrimitives.WriteUInt64BigEndian(Span, A);
			BinaryPrimitives.WriteUInt64BigEndian(Span[8..], B);
			BinaryPrimitives.WriteUInt32BigEndian(Span[16..], C);
		}

		/// <summary>
		/// Test two hash values for equality
		/// </summary>
		public static bool operator ==(IoHash A, IoHash B) => A.Equals(B);

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
			return new IoHash(Hash.Span.Slice(0, NumBytes));
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
			return new IoHash(Reader.ReadFixedLengthBytes(IoHash.NumBytes).Span);
		}

		/// <summary>
		/// Write an <see cref="IoHash"/> to a memory writer
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Hash"></param>
		public static void WriteIoHash(this MemoryWriter Writer, IoHash Hash)
		{
			Hash.CopyTo(Writer.AllocateSpan(IoHash.NumBytes));
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
