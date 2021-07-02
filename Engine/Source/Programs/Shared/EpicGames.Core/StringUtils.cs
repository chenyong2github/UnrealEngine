// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	public static class StringUtils
	{
		/// <summary>
		/// Array mapping from ascii index to hexadecimal digits.
		/// </summary>
		static sbyte[] HexDigits;

		/// <summary>
		/// Hex digits to utf8 byte
		/// </summary>
		static byte[] HexDigitToUtf8Byte = Encoding.UTF8.GetBytes("0123456789abcdef");

		/// <summary>
		/// Array mapping human readable size of bytes, 1024^x. long max is within the range of Exabytes.
		/// </summary>
		static string[] ByteSizes = { "B", "KB", "MB", "GB", "TB", "PB", "EB" };

		/// <summary>
		/// Static constructor. Initializes the HexDigits array.
		/// </summary>
		static StringUtils()
		{
			HexDigits = new sbyte[256];
			for (int Idx = 0; Idx < 256; Idx++)
			{
				HexDigits[Idx] = -1;
			}
			for (int Idx = '0'; Idx <= '9'; Idx++)
			{
				HexDigits[Idx] = (sbyte)(Idx - '0');
			}
			for (int Idx = 'a'; Idx <= 'f'; Idx++)
			{
				HexDigits[Idx] = (sbyte)(10 + Idx - 'a');
			}
			for (int Idx = 'A'; Idx <= 'F'; Idx++)
			{
				HexDigits[Idx] = (sbyte)(10 + Idx - 'A');
			}
		}

		/// <summary>
		/// Indents a string by a given indent
		/// </summary>
		/// <param name="Text">The text to indent</param>
		/// <param name="Indent">The indent to add to each line</param>
		/// <returns>The indented string</returns>
		public static string Indent(string Text, string Indent)
		{
			string Result = "";
			if(Text.Length > 0)
			{
				Result = Indent + Text.Replace("\n", "\n" + Indent);
			}
			return Result;
		}

		/// <summary>
		/// Expand all the property references (of the form $(PropertyName)) in a string.
		/// </summary>
		/// <param name="Text">The input string to expand properties in</param>
		/// <param name="Properties">Dictionary of properties to expand</param>
		/// <returns>The expanded string</returns>
		public static string ExpandProperties(string Text, Dictionary<string, string> Properties)
		{
			return ExpandProperties(Text, Name => { Properties.TryGetValue(Name, out string? Value); return Value; });
		}

		/// <summary>
		/// Expand all the property references (of the form $(PropertyName)) in a string.
		/// </summary>
		/// <param name="Text">The input string to expand properties in</param>
		/// <param name="GetPropertyValue">Delegate to retrieve a property value</param>
		/// <returns>The expanded string</returns>
		public static string ExpandProperties(string Text, Func<string, string?> GetPropertyValue)
		{
			string Result = Text;
			for (int Idx = Result.IndexOf("$("); Idx != -1; Idx = Result.IndexOf("$(", Idx))
			{
				// Find the end of the variable name
				int EndIdx = Result.IndexOf(')', Idx + 2);
				if (EndIdx == -1)
				{
					break;
				}

				// Extract the variable name from the string
				string Name = Result.Substring(Idx + 2, EndIdx - (Idx + 2));

				// Check if we've got a value for this variable
				string? Value = GetPropertyValue(Name);
				if (Value == null)
				{
					// Do not expand it; must be preprocessing the script.
					Idx = EndIdx;
				}
				else
				{
					// Replace the variable, or skip past it
					Result = Result.Substring(0, Idx) + Value + Result.Substring(EndIdx + 1);

					// Make sure we skip over the expanded variable; we don't want to recurse on it.
					Idx += Value.Length;
				}
			}
			return Result;
		}

		/// <summary>
		/// Takes a given sentence and wraps it on a word by word basis so that no line exceeds the
		/// set maximum line length. Words longer than a line are broken up. Returns the sentence as
		/// a list of individual lines.
		/// </summary>
		/// <param name="Text">The text to be wrapped</param>
		/// <param name="MaxWidth">The maximum (non negative) length of the returned sentences</param>
		/// <param name="Lines">Receives a list of word-wrapped lines</param>
		public static List<string> WordWrap(string Text, int MaxWidth)
		{
			// Early out
			if (Text.Length == 0)
			{
				return new List<string>();
			}

			string[] Words = Text.Split(' ');
			List<string> WrappedWords = new List<string>();

			string CurrentSentence = string.Empty;
			foreach (var Word in Words)
			{
				// if this is a very large word, split it
				if (Word.Length > MaxWidth)
				{
					// If the current sentence is ready to be written, do that.
					if (CurrentSentence.Length >= MaxWidth)
					{
						// next line and reset sentence
						WrappedWords.Add(CurrentSentence);
						CurrentSentence = string.Empty;
					}

					// Top up the current line
					WrappedWords.Add(CurrentSentence + Word.Substring(0, MaxWidth - CurrentSentence.Length));

					int length = MaxWidth - CurrentSentence.Length;
					while (length + MaxWidth < Word.Length)
					{
						// Place the starting lengths into their own lines
						WrappedWords.Add(Word.Substring(length, Math.Min(MaxWidth, Word.Length - length)));
						length += MaxWidth;
					}

					// then the trailing end into the next line
					CurrentSentence += Word.Substring(length, Math.Min(MaxWidth, Word.Length - length)) + " ";
				}
				else
				{
					if (CurrentSentence.Length + Word.Length > MaxWidth)
					{
						// next line and reset sentence
						WrappedWords.Add(CurrentSentence);
						CurrentSentence = string.Empty;
					}

					// Add the word to the current sentence.
					CurrentSentence += Word + " ";
				}
			}

			if (CurrentSentence.Length > 0)
			{
				WrappedWords.Add(CurrentSentence);
			}

			return WrappedWords;
		}

		/// <summary>
		/// Extension method to allow formatting a string to a stringbuilder and appending a newline
		/// </summary>
		/// <param name="Builder">The string builder</param>
		/// <param name="Format">Format string, as used for StringBuilder.AppendFormat</param>
		/// <param name="Args">Arguments for the format string</param>
		public static void AppendLine(this StringBuilder Builder, string Format, params object[] Args)
		{
			Builder.AppendFormat(Format, Args);
			Builder.AppendLine();
		}

		/// <summary>
		/// Formats a list of strings in the style "1, 2, 3 and 4"
		/// </summary>
		/// <param name="Arguments">List of strings to format</param>
		/// <param name="Conjunction">Conjunction to use between the last two items in the list (eg. "and" or "or")</param>
		/// <returns>Formatted list of strings</returns>
		public static string FormatList(string[] Arguments, string Conjunction = "and")
		{
			StringBuilder Result = new StringBuilder();
			if (Arguments.Length > 0)
			{
				Result.Append(Arguments[0]);
				for (int Idx = 1; Idx < Arguments.Length; Idx++)
				{
					if (Idx == Arguments.Length - 1)
					{
						Result.AppendFormat(" {0} ", Conjunction);
					}
					else
					{
						Result.Append(", ");
					}
					Result.Append(Arguments[Idx]);
				}
			}
			return Result.ToString();
		}

		/// <summary>
		/// Formats a list of strings in the style "1, 2, 3 and 4"
		/// </summary>
		/// <param name="Arguments">List of strings to format</param>
		/// <param name="Conjunction">Conjunction to use between the last two items in the list (eg. "and" or "or")</param>
		/// <returns>Formatted list of strings</returns>
		public static string FormatList(IEnumerable<string> Arguments, string Conjunction = "and")
		{
			return FormatList(Arguments.ToArray(), Conjunction);
		}


		/// <summary>
		/// Formats a list of items
		/// </summary>
		/// <param name="Items">Array of items</param>
		/// <param name="MaxCount">Maximum number of items to include in the list</param>
		/// <returns>Formatted list of items</returns>
		public static string FormatList(string[] Items, int MaxCount)
		{
			if (Items.Length == 0)
			{
				return "unknown";
			}
			else if (Items.Length == 1)
			{
				return Items[0];
			}
			else if (Items.Length <= MaxCount)
			{
				return $"{String.Join(", ", Items.Take(Items.Length - 1))} and {Items.Last()}";
			}
			else
			{
				return $"{String.Join(", ", Items.Take(MaxCount - 1))} and {Items.Length - (MaxCount - 1)} others";
			}
		}

		/// <summary>
		/// Parses a hexadecimal digit
		/// </summary>
		/// <param name="Character">Character to parse</param>
		/// <returns>Value of this digit, or -1 if invalid</returns>
		public static int GetHexDigit(byte Character)
		{
			return HexDigits[Character];
		}

		/// <summary>
		/// Parses a hexadecimal digit
		/// </summary>
		/// <param name="Character">Character to parse</param>
		/// <returns>Value of this digit, or -1 if invalid</returns>
		public static int GetHexDigit(char Character)
		{
			return HexDigits[Math.Min((uint)Character, 127)];
		}

		/// <summary>
		/// Parses a hexadecimal string into an array of bytes
		/// </summary>
		/// <returns>Array of bytes</returns>
		public static byte[] ParseHexString(string Text)
		{
			byte[]? Bytes;
			if(!TryParseHexString(Text, out Bytes))
			{
				throw new FormatException(String.Format("Invalid hex string: '{0}'", Text));
			}
			return Bytes;
		}

		/// <summary>
		/// Parses a hexadecimal string into an array of bytes
		/// </summary>
		/// <returns>Array of bytes</returns>
		public static byte[] ParseHexString(ReadOnlySpan<byte> Text)
		{
			byte[]? Bytes;
			if (!TryParseHexString(Text, out Bytes))
			{
				throw new FormatException($"Invalid hex string: '{Encoding.UTF8.GetString(Text)}'");
			}
			return Bytes;
		}

		/// <summary>
		/// Parses a hexadecimal string into an array of bytes
		/// </summary>
		/// <param name="Text">Text to parse</param>
		/// <returns></returns>
		public static bool TryParseHexString(string Text, [NotNullWhen(true)] out byte[]? OutBytes)
		{
			if((Text.Length & 1) != 0)
			{
				throw new FormatException("Length of hex string must be a multiple of two characters");
			}

			byte[] Bytes = new byte[Text.Length / 2];
			for(int Idx = 0; Idx < Text.Length; Idx += 2)
			{
				int Value = (GetHexDigit(Text[Idx]) << 4) | GetHexDigit(Text[Idx + 1]);
				if(Value < 0)
				{
					OutBytes = null;
					return false;
				}
				Bytes[Idx / 2] = (byte)Value;
			}
			OutBytes = Bytes;
			return true;
		}

		/// <summary>
		/// Parses a hexadecimal string into an array of bytes
		/// </summary>
		/// <param name="Text">Text to parse</param>
		/// <returns></returns>
		public static bool TryParseHexString(ReadOnlySpan<byte> Text, [NotNullWhen(true)] out byte[]? OutBytes)
		{
			if ((Text.Length & 1) != 0)
			{
				throw new FormatException("Length of hex string must be a multiple of two characters");
			}

			byte[] Bytes = new byte[Text.Length / 2];
			for (int Idx = 0; Idx < Text.Length; Idx += 2)
			{
				int Value = ParseHexByte(Text, Idx);
				if (Value < 0)
				{
					OutBytes = null;
					return false;
				}
				Bytes[Idx / 2] = (byte)Value;
			}
			OutBytes = Bytes;
			return true;
		}

		/// <summary>
		/// Parse a hex byte from the given offset into a span of utf8 characters
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <param name="Idx">Index within the text to parse</param>
		/// <returns>The parsed value, or a negative value on error</returns>
		public static int ParseHexByte(ReadOnlySpan<byte> Text, int Idx)
		{
			return ((int)HexDigits[Text[Idx]] << 4) | ((int)HexDigits[Text[Idx + 1]]);
		}

		/// <summary>
		/// Formats an array of bytes as a hexadecimal string
		/// </summary>
		/// <param name="Bytes">An array of bytes</param>
		/// <returns>String representation of the array</returns>
		public static string FormatHexString(byte[] Bytes)
		{
			return FormatHexString(Bytes.AsSpan());
		}

		/// <summary>
		/// Formats an array of bytes as a hexadecimal string
		/// </summary>
		/// <param name="Bytes">An array of bytes</param>
		/// <returns>String representation of the array</returns>
		public static string FormatHexString(ReadOnlySpan<byte> Bytes)
		{
			const string HexDigits = "0123456789abcdef";

			char[] Characters = new char[Bytes.Length * 2];
			for (int Idx = 0; Idx < Bytes.Length; Idx++)
			{
				Characters[Idx * 2 + 0] = HexDigits[Bytes[Idx] >> 4];
				Characters[Idx * 2 + 1] = HexDigits[Bytes[Idx] & 15];
			}
			return new string(Characters);
		}

		/// <summary>
		/// Formats an array of bytes as a hexadecimal string
		/// </summary>
		/// <param name="Bytes">An array of bytes</param>
		/// <returns>String representation of the array</returns>
		public static Utf8String FormatUtf8HexString(ReadOnlySpan<byte> Bytes)
		{
			byte[] Characters = new byte[Bytes.Length * 2];
			for (int Idx = 0; Idx < Bytes.Length; Idx++)
			{
				Characters[Idx * 2 + 0] = HexDigitToUtf8Byte[Bytes[Idx] >> 4];
				Characters[Idx * 2 + 1] = HexDigitToUtf8Byte[Bytes[Idx] & 15];
			}
			return new Utf8String(Characters);
		}

		/// <summary>
		/// Quotes a string as a command line argument
		/// </summary>
		/// <param name="String">The string to quote</param>
		/// <returns>The quoted argument if it contains any spaces, otherwise the original string</returns>
		public static string QuoteArgument(this string String)
		{
			if (String.Contains(' '))
			{
				return $"\"{String}\"";
			}
			else
			{
				return String;
			}
		}

		/// <summary>
		/// Formats bytes into a human readable string
		/// </summary>
		/// <param name="Bytes">The total number of bytes</param>
		/// <param name="DecimalPlaces">The number of decimal places to round the resulting value</param>
		/// <returns>Human readable string based on the value of Bytes</returns>
		public static string FormatBytesString(long Bytes, int DecimalPlaces = 2)
		{
			if (Bytes == 0)
			{
				return $"0 {ByteSizes[0]}";
			}
			long BytesAbs = Math.Abs(Bytes);
			int Power = Convert.ToInt32(Math.Floor(Math.Log(BytesAbs, 1024)));
			double Value = Math.Round(BytesAbs / Math.Pow(1024, Power), DecimalPlaces);
			return $"{(Math.Sign(Bytes) * Value)} {ByteSizes[Power]}";
		}

		/// <summary>
		/// Converts a bytes string into bytes. E.g 1.2KB -> 1229
		/// </summary>
		/// <param name="BytesString"></param>
		/// <returns></returns>
		public static long ParseBytesString( string BytesString )
		{
			BytesString = BytesString.Trim();

			int Power = ByteSizes.FindIndex( s => (s != ByteSizes[0]) && BytesString.EndsWith(s, StringComparison.InvariantCultureIgnoreCase ) ); // need to handle 'B' suffix separately
			if (Power == -1 && BytesString.EndsWith(ByteSizes[0]))
			{
				Power = 0;
			}
			if (Power != -1)
			{
				BytesString = BytesString.Substring(0, BytesString.Length - ByteSizes[Power].Length );
			}

			double Value = double.Parse(BytesString);
			if (Power > 0 )
			{
				Value *= Math.Pow(1024, Power);
			}

			return (long)Math.Round(Value);
		}

		/// <summary>
		/// Converts a bytes string into bytes. E.g 1.5KB -> 1536
		/// </summary>
		/// <param name="BytesString"></param>
		/// <returns></returns>
		public static bool TryParseBytesString( string BytesString, out long? Bytes )
		{
			try
			{
				Bytes = ParseBytesString(BytesString);
				return true;
			}
			catch(Exception)
			{
			}

			Bytes = null;
			return false;
		}
	}
}
