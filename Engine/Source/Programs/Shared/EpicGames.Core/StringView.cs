// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// View of a character string. Allows comparing/manipulating substrings without unnecessary memory allocatinos.
	/// </summary>
	public readonly struct StringView : IEquatable<StringView>
	{
		/// <summary>
		/// Memory containing the characters
		/// </summary>
		public ReadOnlyMemory<char> Memory { get; }

		/// <summary>
		/// Span for the sequence of characters
		/// </summary>
		public ReadOnlySpan<char> Span 
		{ 
			get { return Memory.Span; } 
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">The memory containing the characters</param>
		public StringView(ReadOnlyMemory<char> Memory)
		{
			this.Memory = Memory;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Text">String to construct from</param>
		public StringView(string Text)
		{
			Memory = Text.AsMemory();
		}

		/// <summary>
		/// Constructs a view onto a substring of the given string
		/// </summary>
		/// <param name="Text">String to construct from</param>
		/// <param name="Index">Offset within the string for this view</param>
		public StringView(string Text, int Index)
		{
			Memory = Text.AsMemory(Index);
		}

		/// <summary>
		/// Constructs a view onto a substring of the given string
		/// </summary>
		/// <param name="Text">String to construct from</param>
		/// <param name="Index">Offset within the string for this view</param>
		/// <param name="Count">Number of characters to include</param>
		public StringView(string Text, int Index, int Count)
		{
			Memory = Text.AsMemory(Index, Count);
		}

		/// <summary>
		/// Equality comparer
		/// </summary>
		/// <param name="X">First string to compare</param>
		/// <param name="Y">Second string to compare</param>
		/// <returns>True if the strings are equal</returns>
		public static bool operator ==(StringView X, StringView Y)
		{
			return X.Equals(Y);
		}

		/// <summary>
		/// Inequality comparer
		/// </summary>
		/// <param name="X">First string to compare</param>
		/// <param name="Y">Second string to compare</param>
		/// <returns>True if the strings are equal</returns>
		public static bool operator !=(StringView X, StringView Y)
		{
			return !X.Equals(Y);
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj)
		{
			return Obj is StringView && Equals((StringView)Obj);
		}

		/// <inheritdoc/>
		public override int GetHashCode()
		{
			return String.GetHashCode(Memory.Span);
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return new string(Memory.Span);
		}

		/// <inheritdoc/>
		public bool Equals(StringView Other)
		{
			return Equals(Other, StringComparison.CurrentCulture);
		}

		/// <inheritdoc/>
		public bool Equals(StringView? Other, StringComparison comparisonType)
		{
			return Other.HasValue && Memory.Span.Equals(Other.Value.Memory.Span, comparisonType);
		}

		/// <summary>
		/// Implicit conversion operator from a regular string
		/// </summary>
		/// <param name="Text">The string to construct from</param>
		public static implicit operator StringView(string Text) => new StringView(Text);
	}

	/// <summary>
	/// Comparer for StringView objects
	/// </summary>
	public class StringViewComparer : IComparer<StringView>, IEqualityComparer<StringView>
	{
		/// <summary>
		/// Static instance of an ordinal StringView comparer
		/// </summary>
		public static StringViewComparer Ordinal = new StringViewComparer(StringComparison.Ordinal);

		/// <summary>
		/// Static instance of an ordinal StringView comparer which ignores case
		/// </summary>
		public static StringViewComparer OrdinalIgnoreCase = new StringViewComparer(StringComparison.OrdinalIgnoreCase);

		/// <summary>
		/// The comparison type
		/// </summary>
		public StringComparison ComparisonType { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ComparisonType">Type of comparison to perform</param>
		public StringViewComparer(StringComparison ComparisonType)
		{
			this.ComparisonType = ComparisonType;
		}

		/// <inheritdoc/>
		public bool Equals(StringView X, StringView Y)
		{
			return X.Span.Equals(Y.Span, ComparisonType);
		}

		/// <inheritdoc/>
		public int GetHashCode(StringView obj)
		{
			return String.GetHashCode(obj.Span);
		}

		/// <inheritdoc/>
		public int Compare(StringView X, StringView Y)
		{
			return X.Span.CompareTo(Y.Span, ComparisonType);
		}
	}
}
