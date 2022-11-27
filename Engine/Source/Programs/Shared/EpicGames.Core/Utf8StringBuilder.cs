// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Represents a memory region which can be treated as a utf-8 string.
	/// </summary>
	public struct Utf8StringBuilder
	{
		readonly ArrayMemoryWriter _writer;

		/// <summary>
		/// Returns the length of this string
		/// </summary>
		public int Length => _writer.Length;

		/// <summary>
		/// Accessor for the written span
		/// </summary>
		public Span<byte> WrittenSpan => _writer.WrittenSpan;

		/// <summary>
		/// Accessor for the written memory
		/// </summary>
		public Memory<byte> WrittenMemory => _writer.WrittenMemory;

		/// <summary>
		/// Accessor for the written data
		/// </summary>
		public Utf8String WrittenString => new Utf8String(_writer.WrittenMemory);

		/// <summary>
		/// Constructor
		/// </summary>
		public Utf8StringBuilder()
			: this(256)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public Utf8StringBuilder(int initialSize)
			: this(new ArrayMemoryWriter(initialSize))
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public Utf8StringBuilder(ArrayMemoryWriter writer)
		{
			_writer = writer;
		}

		/// <summary>
		/// Append a character to the end of this builder
		/// </summary>
		/// <param name="ch">Character to append</param>
		public void Append(byte ch)
		{
			_writer.WriteUInt8(ch);
		}

		/// <summary>
		/// Appends a string to the end of this builder
		/// </summary>
		/// <param name="text">Text to append</param>
		public void Append(Utf8String text)
		{
			_writer.WriteFixedLengthBytes(text.Span);
		}

		/// <summary>
		/// Converts the written memory to a utf8 string
		/// </summary>
		/// <returns></returns>
		public Utf8String ToUtf8String()
		{
			return new Utf8String(_writer.WrittenMemory);
		}

		/// <inheritdoc/>
		public override string ToString() => ToUtf8String().ToString();
	}
}
