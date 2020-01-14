// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Tools.DotNETCommon
{
	public static class StringUtils
	{
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
		/// <returns>Formatted list of strings</returns>
		public static string FormatList(string[] Arguments)
		{
			StringBuilder Result = new StringBuilder();
			if(Arguments.Length > 0)
			{
				Result.Append(Arguments[0]);
				for(int Idx = 1; Idx < Arguments.Length; Idx++)
				{
					if(Idx == Arguments.Length - 1)
					{
						Result.Append(" and ");
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
		/// <returns>Formatted list of strings</returns>
		public static string FormatList(IEnumerable<string> Arguments)
		{
			return FormatList(Arguments.ToArray());
		}

		/// <summary>
		/// Parses a hexadecimal digit
		/// </summary>
		/// <param name="Character">Character to parse</param>
		/// <returns>Value of this digit</returns>
		public static bool TryParseHexDigit(char Character, out int Value)
		{
			if(Character >= '0' && Character <= '9')
			{
				Value = Character - '0';
				return true;
			}
			else if(Character >= 'a' && Character <= 'f')
			{
				Value = 10 + (Character - 'a');
				return true;
			}
			else if(Character >= 'A' && Character <= 'F')
			{
				Value = 10 + (Character - 'A');
				return true;
			}
			else
			{
				Value = 0;
				return false;
			}
		}

		/// <summary>
		/// Parses a hexadecimal string into an array of bytes
		/// </summary>
		/// <returns>Array of bytes</returns>
		public static byte[] ParseHexString(string Text)
		{
			byte[] Bytes;
			if(!TryParseHexString(Text, out Bytes))
			{
				throw new FormatException(String.Format("Invalid hex string: '{0}'", Text));
			}
			return Bytes;
		}

		/// <summary>
		/// Parses a hexadecimal string into an array of bytes
		/// </summary>
		/// <param name="Text">Text to parse</param>
		/// <returns></returns>
		public static bool TryParseHexString(string Text, out byte[] OutBytes)
		{
			if((Text.Length & 1) != 0)
			{
				throw new FormatException("Length of hex string must be a multiple of two characters");
			}

			byte[] Bytes = new byte[Text.Length / 2];
			for(int Idx = 0; Idx < Text.Length; Idx += 2)
			{
				int A, B;
				if(!TryParseHexDigit(Text[Idx], out A) || !TryParseHexDigit(Text[Idx + 1], out B))
				{
					OutBytes = null;
					return false;
				}
				Bytes[Idx / 2] = (byte)((A << 4) | B);
			}
			OutBytes = Bytes;
			return true;
		}

		/// <summary>
		/// Formats an array of bytes as a hexadecimal string
		/// </summary>
		/// <param name="Bytes">An array of bytes</param>
		/// <returns>String representation of the array</returns>
		public static string FormatHexString(byte[] Bytes)
		{
			const string HexDigits = "0123456789abcdef";

			char[] Characters = new char[Bytes.Length * 2];
			for(int Idx = 0; Idx < Bytes.Length; Idx++)
			{
				Characters[Idx * 2 + 0] = HexDigits[Bytes[Idx] >> 4];
				Characters[Idx * 2 + 1] = HexDigits[Bytes[Idx] & 15];
			}
			return new string(Characters);
		}
	}
}
