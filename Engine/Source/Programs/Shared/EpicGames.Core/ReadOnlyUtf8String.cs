// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Represents a memory region which can be treated as a utf-8 string.
	/// </summary>
	public struct ReadOnlyUtf8String : IEquatable<ReadOnlyUtf8String>
	{
		/// <summary>
		/// An empty string
		/// </summary>
		public static readonly ReadOnlyUtf8String Empty = new ReadOnlyUtf8String();

		/// <summary>
		/// The data represented by this string
		/// </summary>
		public ReadOnlyMemory<byte> Memory { get; }

		/// <summary>
		/// Returns read only span for this string
		/// </summary>
		public ReadOnlySpan<byte> Span
		{
			get { return Memory.Span; } 
		}

		/// <summary>
		/// Determines if this string is empty
		/// </summary>
		public bool IsEmpty
		{
			get { return Memory.IsEmpty; }
		}

		/// <summary>
		/// Returns the length of this string
		/// </summary>
		public int Length
		{
			get { return Memory.Length; }
		}

		/// <summary>
		/// Allows indexing individual bytes of the data
		/// </summary>
		/// <param name="Index">Byte index</param>
		/// <returns>Byte at the given index</returns>
		public byte this[int Index]
		{
			get { return Span[Index]; }
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Text">Text to construct from</param>
		public ReadOnlyUtf8String(string Text)
		{
			this.Memory = Encoding.UTF8.GetBytes(Text);
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">The data to construct from</param>
		public ReadOnlyUtf8String(ReadOnlyMemory<byte> Memory)
		{
			this.Memory = Memory;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Buffer">The buffer to construct from</param>
		/// <param name="Offset">Offset within the buffer</param>
		/// <param name="Length">Length of the string within the buffer</param>
		public ReadOnlyUtf8String(byte[] Buffer, int Offset, int Length)
		{
			this.Memory = new ReadOnlyMemory<byte>(Buffer, Offset, Length);
		}

		/// <summary>
		/// Tests two strings for equality
		/// </summary>
		/// <param name="A">The first string to compare</param>
		/// <param name="B">The second string to compare</param>
		/// <returns>True if the strings are equal</returns>
		public static bool operator ==(ReadOnlyUtf8String A, ReadOnlyUtf8String B)
		{
			return A.Equals(B);
		}

		/// <summary>
		/// Tests two strings for inequality
		/// </summary>
		/// <param name="A">The first string to compare</param>
		/// <param name="B">The second string to compare</param>
		/// <returns>True if the strings are not equal</returns>
		public static bool operator !=(ReadOnlyUtf8String A, ReadOnlyUtf8String B)
		{
			return !A.Equals(B);
		}

		/// <summary>
		/// Tests whether two strings are equal
		/// </summary>
		/// <param name="Other">The other string to compare to</param>
		/// <returns>True if the strings are equal</returns>
		public bool Equals(ReadOnlyUtf8String Other)
		{
			return Span.SequenceEqual(Other.Span);
		}

		/// <summary>
		/// Tests if this string starts with another string
		/// </summary>
		/// <param name="Other">The string to check against</param>
		/// <returns>True if this string starts with the other string</returns>
		public bool StartsWith(ReadOnlyUtf8String Other)
		{
			return Span.StartsWith(Other.Span);
		}

		/// <summary>
		/// Tests if this string ends with another string
		/// </summary>
		/// <param name="Other">The string to check against</param>
		/// <returns>True if this string ends with the other string</returns>
		public bool EndsWith(ReadOnlyUtf8String Other)
		{
			return Span.EndsWith(Other.Span);
		}

		/// <summary>
		/// Slices this string at the given start position
		/// </summary>
		/// <param name="Start">Start position</param>
		/// <returns>String starting at the given position</returns>
		public ReadOnlyUtf8String Slice(int Start)
		{
			return new ReadOnlyUtf8String(Memory.Slice(Start));
		}

		/// <summary>
		/// Slices this string at the given start position and length
		/// </summary>
		/// <param name="Start">Start position</param>
		/// <returns>String starting at the given position</returns>
		public ReadOnlyUtf8String Slice(int Start, int Count)
		{
			return new ReadOnlyUtf8String(Memory.Slice(Start, Count));
		}

		/// <summary>
		/// Tests if this string is equal to the other object
		/// </summary>
		/// <param name="obj">Object to compare to</param>
		/// <returns>True if the objects are equivalent</returns>
		public override bool Equals(object? obj)
		{
			ReadOnlyUtf8String? Other = obj as ReadOnlyUtf8String?;
			return Other != null && Equals(Other.Value);
		}

		/// <summary>
		/// Returns the hash code of this string
		/// </summary>
		/// <returns>Hash code for the string</returns>
		public override int GetHashCode()
		{
			int Hash = 5381;
			for(int Idx = 0; Idx < Memory.Length; Idx++)
			{
				Hash += (Hash << 5) + Span[Idx];
			}
			return Hash;
		}

		/// <summary>
		/// Gets the string represented by this data
		/// </summary>
		/// <returns>The utf-8 string</returns>
		public override string ToString()
		{
			return Encoding.UTF8.GetString(Span);
		}

		/// <summary>
		/// Converts a string to a utf-8 string
		/// </summary>
		/// <param name="Text">Text to convert</param>
		public static implicit operator ReadOnlyUtf8String(string Text)
		{
			return new ReadOnlyUtf8String(new ReadOnlyMemory<byte>(Encoding.UTF8.GetBytes(Text)));
		}
	}

	/// <summary>
	/// Comparison classes for utf8 strings
	/// </summary>
	public abstract class ReadOnlyUtf8StringComparer : IEqualityComparer<ReadOnlyUtf8String>
	{
		/// <summary>
		/// Ordinal comparer for utf8 strings
		/// </summary>
		class OrdinalComparer : ReadOnlyUtf8StringComparer
		{
			/// <inheritdoc/>
			public override bool Equals(ReadOnlyUtf8String StrA, ReadOnlyUtf8String StrB)
			{
				return StrA.Equals(StrB);
			}

			/// <inheritdoc/>
			public override int GetHashCode(ReadOnlyUtf8String String)
			{
				return String.GetHashCode();
			}
		}

		/// <summary>
		/// Comparison between ReadOnlyUtf8String objects that ignores case for ASCII characters
		/// </summary>
		class OrdinalIgnoreCaseComparer : ReadOnlyUtf8StringComparer 
		{
			/// <inheritdoc/>
			public override bool Equals(ReadOnlyUtf8String StrA, ReadOnlyUtf8String StrB)
			{
				if (StrA.Length != StrB.Length)
				{
					return false;
				}

				for (int Idx = 0; Idx < StrA.Length; Idx++)
				{
					if (StrA[Idx] != StrB[Idx] && ToUpper(StrA[Idx]) != ToUpper(StrB[Idx]))
					{
						return false;
					}
				}

				return true;
			}

			/// <inheritdoc/>
			public override int GetHashCode(ReadOnlyUtf8String String)
			{
				HashCode HashCode = new HashCode();
				for (int Idx = 0; Idx < String.Length; Idx++)
				{
					HashCode.Add(ToUpper(String[Idx]));
				}
				return HashCode.ToHashCode();
			}

			/// <summary>
			/// Convert a character to uppercase
			/// </summary>
			/// <param name="Character">Character to convert</param>
			/// <returns>The uppercase version of the character</returns>
			static byte ToUpper(byte Character)
			{
				return (Character >= 'a' && Character <= 'z') ? (byte)(Character - 'a' + 'A') : Character;
			}
		}

		/// <summary>
		/// Static instance of the ordinal utf8 ordinal comparer
		/// </summary>
		public static ReadOnlyUtf8StringComparer Ordinal = new OrdinalComparer();

		/// <summary>
		/// Static instance of the case-insensitive utf8 ordinal string comparer
		/// </summary>
		public static ReadOnlyUtf8StringComparer OrdinalIgnoreCase = new OrdinalIgnoreCaseComparer();

		/// <inheritdoc/>
		public abstract bool Equals(ReadOnlyUtf8String StrA, ReadOnlyUtf8String StrB);

		/// <inheritdoc/>
		public abstract int GetHashCode(ReadOnlyUtf8String String);
	}

	/// <summary>
	/// Extension methods for ReadOnlyUtf8String objects
	/// </summary>
	public static class MemoryWriterExtensions
	{
		/// <summary>
		/// Reads a null-terminated utf8 string from the buffer
		/// </summary>
		/// <returns>The string data</returns>
		public static ReadOnlyUtf8String ReadString(this MemoryReader Reader)
		{
			ReadOnlySpan<byte> Span = Reader.Span;
			int Length = Span.IndexOf((byte)0);
			ReadOnlyUtf8String Value = new ReadOnlyUtf8String(Reader.ReadFixedLengthBytes(Length));
			Reader.ReadInt8();
			return Value;
		}

		/// <summary>
		/// Writes a UTF8 string into memory with a null terminator
		/// </summary>
		/// <param name="Writer">The memory writer to serialize to</param>
		/// <param name="String">String to write</param>
		public static void WriteString(this MemoryWriter Writer, ReadOnlyUtf8String String)
		{
			Writer.WriteFixedLengthBytes(String.Span);
			Writer.WriteInt8(0);
		}

		/// <summary>
		/// Determines the size of a serialized utf-8 string
		/// </summary>
		/// <param name="String">The string to measure</param>
		/// <returns>Size of the serialized string</returns>
		public static int GetSerializedSize(this ReadOnlyUtf8String String)
		{
			return String.Length + 1;
		}
	}
}
