// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Buffers.Binary;
using System.Buffers.Text;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;
using System.Text.Unicode;

namespace EpicGames.Perforce
{
	/// <summary>
	/// The type of a value returned by Perforce
	/// </summary>
	public enum PerforceValueType
	{
		/// <summary>
		/// A utf-8 encoded string
		/// </summary>
		String,

		/// <summary>
		/// A 32-bit integer
		/// </summary>
		Integer,
	}

	/// <summary>
	/// Wrapper for values returned by Perforce
	/// </summary>
	public struct PerforceValue
	{
		/// <summary>
		/// The raw data for the value, including type, size, and payload
		/// </summary>
		public readonly ReadOnlyMemory<byte> Data;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data">The data to construct this value from</param>
		public PerforceValue(ReadOnlyMemory<byte> Data)
		{
			this.Data = Data;
		}

		/// <summary>
		/// Determines if the value is empty
		/// </summary>
		/// <returns>True if the value is empty</returns>
		public bool IsEmpty
		{
			get { return Data.IsEmpty; }
		}

		/// <summary>
		/// Accessor for the type of value stored by this struct
		/// </summary>
		public PerforceValueType Type
		{
			get
			{
				switch (Data.Span[0])
				{
					case (byte)'s':
						return PerforceValueType.String;
					case (byte)'i':
						return PerforceValueType.Integer;
					default:
						throw new NotImplementedException("Unknown/unsupported value type");
				}
			}
		}

		/// <summary>
		/// Converts the value to a boolean
		/// </summary>
		/// <returns>The boolean value</returns>
		public bool AsBool()
		{
			Utf8String String = GetString();
			return String.Length == 0 || String == StringConstants.True;
		}

		/// <summary>
		/// Converts the value to an integer
		/// </summary>
		/// <returns>Integer value</returns>
		public int AsInteger()
		{
			if (Type == PerforceValueType.Integer)
			{
				return GetInteger();
			}
			else if (Type == PerforceValueType.String)
			{
				Utf8String String = GetString();

				int Value;
				int BytesConsumed;
				if (Utf8Parser.TryParse(String.Span, out Value, out BytesConsumed) && BytesConsumed == String.Length)
				{
					return Value;
				}
				else if (String == StringConstants.New || String == StringConstants.None)
				{
					return -1;
				}
				else if (String.Length > 0 && String[0] == '#' && Utf8Parser.TryParse(String.Span.Slice(1), out Value, out BytesConsumed) && BytesConsumed + 1 == String.Length)
				{
					return Value;
				}
				else if (String == StringConstants.Default)
				{
					return PerforceReflection.DefaultChange;
				}
				else
				{
					throw new PerforceException($"Unable to parse {String} as an integer");
				}
			}
			else
			{
				throw new NotImplementedException();
			}
		}

		/// <summary>
		/// Converts this value to a long
		/// </summary>
		/// <returns>The converted value</returns>
		public long AsLong()
		{
			Utf8String String = AsString();

			long Value;
			int BytesConsumed;
			if (!Utf8Parser.TryParse(AsString().Span, out Value, out BytesConsumed) || BytesConsumed != String.Length)
			{
				throw new PerforceException($"Unable to parse {ToString()} as a long value");
			}

			return Value;
		}

		/// <summary>
		/// Converts this value to a DateTimeOffset
		/// </summary>
		/// <returns>The converted value</returns>
		public DateTimeOffset AsDateTimeOffset()
		{
			string Text = ToString();
			return DateTimeOffset.Parse(Regex.Replace(Text, "[a-zA-Z ]*$", "")); // Strip timezone name (eg. "EST")
		}

		/// <summary>
		/// Converts this value to a string
		/// </summary>
		/// <returns>The converted value</returns>
		public Utf8String AsString()
		{
			switch(Type)
			{
				case PerforceValueType.String:
					return GetString();
				case PerforceValueType.Integer:
					return new Utf8String(GetInteger().ToString());
				default:
					throw new NotImplementedException();
			}
		}

		/// <summary>
		/// Gets the contents of this string
		/// </summary>
		/// <returns></returns>
		public int GetInteger()
		{
			if (Type != PerforceValueType.Integer)
			{
				throw new InvalidOperationException("Value is not an integer");
			}
			return BinaryPrimitives.ReadInt32LittleEndian(Data.Span.Slice(1));
		}

		/// <summary>
		/// Gets the contents of this string
		/// </summary>
		/// <returns></returns>
		public Utf8String GetString()
		{
			if (IsEmpty)
			{
				return Utf8String.Empty;
			}
			if (Type != PerforceValueType.String)
			{
				throw new InvalidOperationException("Value is not a string");
			}
			return new Utf8String(Data.Slice(5));
		}

		/// <summary>
		/// Convert to a string
		/// </summary>
		/// <returns>String representation of the value</returns>
		public override string ToString()
		{
			return AsString().ToString();
		}
	}

	/// <summary>
	/// Low-overhead record type for generic responses
	/// </summary>
	public struct PerforceRecord
	{
		/// <summary>
		/// The rows in this record
		/// </summary>
		public List<KeyValuePair<Utf8String, PerforceValue>> Rows;

		/// <summary>
		/// Copy this record into the given array of values. This method is O(n) if every record key being in the list of keys in the same order, but O(n^2) if not.
		/// </summary>
		/// <param name="Keys">List of keys to parse</param>
		/// <param name="Values">Array to receive the list of values</param>
		public void CopyInto(Utf8String[] Keys, PerforceValue[] Values)
		{
			// Clear out the current values array
			for (int ValueIdx = 0; ValueIdx < Values.Length; ValueIdx++)
			{
				Values[ValueIdx] = new PerforceValue();
			}

			// Parse all the keys
			int RowIdx = 0;
			while (RowIdx < Rows.Count)
			{
				int InitialRowIdx = RowIdx;

				// Find the key that matches this row.
				Utf8String RowKey = Rows[RowIdx].Key;
				for (int KeyIdx = 0; KeyIdx < Keys.Length; KeyIdx++)
				{
					Utf8String Key = Keys[KeyIdx];
					if (RowKey == Key)
					{
						// Copy the value to the output array, then move to the next row, and try to match that against the next key.
						Values[KeyIdx] = Rows[RowIdx].Value;
						if (++RowIdx == Rows.Count)
						{
							break;
						}
						RowKey = Rows[RowIdx].Key;
					}
				}

				// If we didn't match any key name for this row, move to the next one
				if(RowIdx == InitialRowIdx)
				{
					RowIdx++;
				}
			}
		}
	}
}
