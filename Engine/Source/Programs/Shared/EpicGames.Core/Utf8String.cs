// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Represents a memory region which can be treated as a utf-8 string.
	/// </summary>
	public struct Utf8String : IEquatable<Utf8String>, IComparable<Utf8String>
	{
		/// <summary>
		/// An empty string
		/// </summary>
		public static readonly Utf8String Empty = new Utf8String();

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
		public Utf8String(string Text)
		{
			this.Memory = Encoding.UTF8.GetBytes(Text);
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">The data to construct from</param>
		public Utf8String(ReadOnlyMemory<byte> Memory)
		{
			this.Memory = Memory;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Buffer">The buffer to construct from</param>
		/// <param name="Offset">Offset within the buffer</param>
		/// <param name="Length">Length of the string within the buffer</param>
		public Utf8String(byte[] Buffer, int Offset, int Length)
		{
			this.Memory = new ReadOnlyMemory<byte>(Buffer, Offset, Length);
		}

		/// <summary>
		/// Duplicate this string
		/// </summary>
		/// <returns></returns>
		public Utf8String Clone()
		{
			byte[] NewBuffer = new byte[Memory.Length];
			Memory.CopyTo(NewBuffer);
			return new Utf8String(NewBuffer);
		}

		/// <summary>
		/// Tests two strings for equality
		/// </summary>
		/// <param name="A">The first string to compare</param>
		/// <param name="B">The second string to compare</param>
		/// <returns>True if the strings are equal</returns>
		public static bool operator ==(Utf8String A, Utf8String B)
		{
			return A.Equals(B);
		}

		/// <summary>
		/// Tests two strings for inequality
		/// </summary>
		/// <param name="A">The first string to compare</param>
		/// <param name="B">The second string to compare</param>
		/// <returns>True if the strings are not equal</returns>
		public static bool operator !=(Utf8String A, Utf8String B)
		{
			return !A.Equals(B);
		}

		/// <inheritdoc/>
		public bool Equals(Utf8String Other) => Utf8StringComparer.Ordinal.Equals(Span, Other.Span);

		/// <inheritdoc/>
		public int CompareTo(Utf8String Other) => Utf8StringComparer.Ordinal.Compare(Span, Other.Span);

		/// <inheritdoc cref="String.Contains(string)"/>
		public bool Contains(Utf8String String) => IndexOf(String) != -1;

		/// <inheritdoc cref="String.Contains(string, StringComparison)"/>
		public bool Contains(Utf8String String, Utf8StringComparer Comparer) => IndexOf(String, Comparer) != -1;

		/// <inheritdoc cref="String.IndexOf(char)"/>
		public int IndexOf(byte Char)
		{
			return Span.IndexOf(Char);
		}

		/// <inheritdoc cref="String.IndexOf(char)"/>
		public int IndexOf(char Char)
		{
			if (Char < 0x80)
			{
				return Span.IndexOf((byte)Char);
			}
			else
			{
				return Span.IndexOf(Encoding.UTF8.GetBytes(new[] { Char }));
			}
		}

		/// <inheritdoc cref="String.IndexOf(char, int)"/>
		public int IndexOf(char Char, int Index) => IndexOf(Char, Index, Length - Index);

		/// <inheritdoc cref="String.IndexOf(char, int, int)"/>
		public int IndexOf(char Char, int Index, int Count)
		{
			int Result;
			if (Char < 0x80)
			{
				Result = Span.Slice(Index, Count).IndexOf((byte)Char);
			}
			else
			{
				Result = Span.Slice(Index, Count).IndexOf(Encoding.UTF8.GetBytes(new[] { Char }));
			}
			return (Result == -1) ? -1 : Result + Index;
		}

		/// <inheritdoc cref="String.IndexOf(string)"/>
		public int IndexOf(Utf8String String)
		{
			return Span.IndexOf(String.Span);
		}

		/// <inheritdoc cref="String.IndexOf(string, StringComparison)"/>
		public int IndexOf(Utf8String String, Utf8StringComparer Comparer)
		{
			for (int Idx = 0; Idx < Length - String.Length; Idx++)
			{
				if (Comparer.Equals(String.Slice(Idx, String.Length), String))
				{
					return Idx;
				}
			}
			return -1;
		}

		/// <inheritdoc cref="String.LastIndexOf(char)"/>
		public int LastIndexOf(byte Char)
		{
			return Span.IndexOf(Char);
		}

		/// <inheritdoc cref="String.LastIndexOf(char)"/>
		public int LastIndexOf(char Char)
		{
			if (Char < 0x80)
			{
				return Span.IndexOf((byte)Char);
			}
			else
			{
				return Span.IndexOf(Encoding.UTF8.GetBytes(new[] { Char }));
			}
		}

		/// <summary>
		/// Tests if this string starts with another string
		/// </summary>
		/// <param name="Other">The string to check against</param>
		/// <returns>True if this string starts with the other string</returns>
		public bool StartsWith(Utf8String Other)
		{
			return Span.StartsWith(Other.Span);
		}

		/// <summary>
		/// Tests if this string ends with another string
		/// </summary>
		/// <param name="Other">The string to check against</param>
		/// <param name="Comparer">The string comparer</param>
		/// <returns>True if this string ends with the other string</returns>
		public bool StartsWith(Utf8String Other, Utf8StringComparer Comparer)
		{
			return Length >= Other.Length && Comparer.Equals(Slice(0, Other.Length), Other);
		}

		/// <summary>
		/// Tests if this string ends with another string
		/// </summary>
		/// <param name="Other">The string to check against</param>
		/// <returns>True if this string ends with the other string</returns>
		public bool EndsWith(Utf8String Other)
		{
			return Span.EndsWith(Other.Span);
		}

		/// <summary>
		/// Tests if this string ends with another string
		/// </summary>
		/// <param name="Other">The string to check against</param>
		/// <param name="Comparer">The string comparer</param>
		/// <returns>True if this string ends with the other string</returns>
		public bool EndsWith(Utf8String Other, Utf8StringComparer Comparer)
		{
			return Length >= Other.Length && Comparer.Equals(Slice(Length - Other.Length), Other);
		}

		/// <inheritdoc cref="Substring(int)"/>
		public Utf8String Slice(int Start) => Substring(Start);

		/// <inheritdoc cref="Substring(int, int)"/>
		public Utf8String Slice(int Start, int Count) => Substring(Start, Count);

		/// <inheritdoc cref="String.Substring(int)"/>
		public Utf8String Substring(int Start)
		{
			return new Utf8String(Memory.Slice(Start));
		}

		/// <inheritdoc cref="String.Substring(int, int)"/>
		public Utf8String Substring(int Start, int Count)
		{
			return new Utf8String(Memory.Slice(Start, Count));
		}
		
		/// <summary>
		/// Tests if this string is equal to the other object
		/// </summary>
		/// <param name="obj">Object to compare to</param>
		/// <returns>True if the objects are equivalent</returns>
		public override bool Equals(object? obj)
		{
			Utf8String? Other = obj as Utf8String?;
			return Other != null && Equals(Other.Value);
		}

		/// <summary>
		/// Returns the hash code of this string
		/// </summary>
		/// <returns>Hash code for the string</returns>
		public override int GetHashCode() => Utf8StringComparer.Ordinal.GetHashCode(Span);

		/// <summary>
		/// Gets the string represented by this data
		/// </summary>
		/// <returns>The utf-8 string</returns>
		public override string ToString()
		{
			return Encoding.UTF8.GetString(Span);
		}

		/// <summary>
		/// Parse a string as an unsigned integer
		/// </summary>
		/// <param name="Text"></param>
		/// <returns></returns>
		public static uint ParseUnsignedInt(Utf8String Text)
		{
			ReadOnlySpan<byte> Bytes = Text.Span;
			if (Bytes.Length == 0)
			{
				throw new Exception("Cannot parse empty string as an integer");
			}

			uint Value = 0;
			for (int Idx = 0; Idx < Bytes.Length; Idx++)
			{
				uint Digit = (uint)(Bytes[Idx] - '0');
				if (Digit > 9)
				{
					throw new Exception($"Cannot parse '{Text}' as an integer");
				}
				Value = (Value * 10) + Digit;
			}
			return Value;
		}

		/// <summary>
		/// Appends two strings
		/// </summary>
		/// <param name="A"></param>
		/// <param name="B"></param>
		/// <returns></returns>
		public static Utf8String operator +(Utf8String A, Utf8String B)
		{
			if (A.Length == 0)
			{
				return B;
			}
			if (B.Length == 0)
			{
				return A;
			}

			byte[] Buffer = new byte[A.Length + B.Length];
			A.Span.CopyTo(Buffer);
			B.Span.CopyTo(Buffer.AsSpan(A.Length));
			return new Utf8String(Buffer);
		}

		/// <summary>
		/// Converts a string to a utf-8 string
		/// </summary>
		/// <param name="Text">Text to convert</param>
		public static implicit operator Utf8String(string Text)
		{
			return new Utf8String(new ReadOnlyMemory<byte>(Encoding.UTF8.GetBytes(Text)));
		}
	}

	/// <summary>
	/// Comparison classes for utf8 strings
	/// </summary>
	public abstract class Utf8StringComparer : IEqualityComparer<Utf8String>, IComparer<Utf8String>
	{
		/// <summary>
		/// Ordinal comparer for utf8 strings
		/// </summary>
		public sealed class OrdinalComparer : Utf8StringComparer
		{
			/// <inheritdoc/>
			public override bool Equals(ReadOnlySpan<byte> StrA, ReadOnlySpan<byte> StrB)
			{
				return StrA.SequenceEqual(StrB);
			}

			/// <inheritdoc/>
			public override int GetHashCode(ReadOnlySpan<byte> String)
			{
				int Hash = 5381;
				for (int Idx = 0; Idx < String.Length; Idx++)
				{
					Hash += (Hash << 5) + String[Idx];
				}
				return Hash;
			}

			public override int Compare(ReadOnlySpan<byte> StrA, ReadOnlySpan<byte> StrB)
			{
				return StrA.SequenceCompareTo(StrB);
			}
		}

		/// <summary>
		/// Comparison between ReadOnlyUtf8String objects that ignores case for ASCII characters
		/// </summary>
		public sealed class OrdinalIgnoreCaseComparer : Utf8StringComparer 
		{
			/// <inheritdoc/>
			public override bool Equals(ReadOnlySpan<byte> StrA, ReadOnlySpan<byte> StrB)
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
			public override int GetHashCode(ReadOnlySpan<byte> String)
			{
				HashCode HashCode = new HashCode();
				for (int Idx = 0; Idx < String.Length; Idx++)
				{
					HashCode.Add(ToUpper(String[Idx]));
				}
				return HashCode.ToHashCode();
			}

			/// <inheritdoc/>
			public override int Compare(ReadOnlySpan<byte> SpanA, ReadOnlySpan<byte> SpanB)
			{
				int Length = Math.Min(SpanA.Length, SpanB.Length);
				for (int Idx = 0; Idx < Length; Idx++)
				{
					if (SpanA[Idx] != SpanB[Idx])
					{
						int UpperA = ToUpper(SpanA[Idx]);
						int UpperB = ToUpper(SpanB[Idx]);
						if (UpperA != UpperB)
						{
							return UpperA - UpperB;
						}
					}
				}
				return SpanA.Length - SpanB.Length;
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
		public static Utf8StringComparer Ordinal { get; } = new OrdinalComparer();

		/// <summary>
		/// Static instance of the case-insensitive utf8 ordinal string comparer
		/// </summary>
		public static Utf8StringComparer OrdinalIgnoreCase { get; } = new OrdinalIgnoreCaseComparer();

		/// <inheritdoc/>
		public bool Equals(Utf8String StrA, Utf8String StrB) => Equals(StrA.Span, StrB.Span);

		/// <inheritdoc/>
		public abstract bool Equals(ReadOnlySpan<byte> StrA, ReadOnlySpan<byte> StrB);

		/// <inheritdoc/>
		public int GetHashCode(Utf8String String) => GetHashCode(String.Span);

		/// <inheritdoc/>
		public abstract int GetHashCode(ReadOnlySpan<byte> String);

		/// <inheritdoc/>
		public int Compare(Utf8String StrA, Utf8String StrB) => Compare(StrA.Span, StrB.Span);

		/// <inheritdoc/>
		public abstract int Compare(ReadOnlySpan<byte> StrA, ReadOnlySpan<byte> StrB);
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
		public static Utf8String ReadString(this MemoryReader Reader)
		{
			ReadOnlySpan<byte> Span = Reader.Span;
			int Length = Span.IndexOf((byte)0);
			Utf8String Value = new Utf8String(Reader.ReadFixedLengthBytes(Length));
			Reader.ReadInt8();
			return Value;
		}

		/// <summary>
		/// Writes a UTF8 string into memory with a null terminator
		/// </summary>
		/// <param name="Writer">The memory writer to serialize to</param>
		/// <param name="String">String to write</param>
		public static void WriteString(this MemoryWriter Writer, Utf8String String)
		{
			Writer.WriteFixedLengthBytes(String.Span);
			Writer.WriteInt8(0);
		}

		/// <summary>
		/// Determines the size of a serialized utf-8 string
		/// </summary>
		/// <param name="String">The string to measure</param>
		/// <returns>Size of the serialized string</returns>
		public static int GetSerializedSize(this Utf8String String)
		{
			return String.Length + 1;
		}
	}
}
