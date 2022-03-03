// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Text;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// Collection of helper methods to emulate UHT string functions
	/// </summary>
	public static class UhtFCString
	{

		/// <summary>
		/// Delimiter between the subobjects
		/// </summary>
		public static char SubObjectDelimiter = ':';

		/// <summary>
		/// Test to see if the string is a boolean
		/// </summary>
		/// <param name="Value">Boolean to test</param>
		/// <returns>True if the value is true</returns>
		public static bool ToBool(StringView Value)
		{
			ReadOnlySpan<char> Span = Value.Span;
			if (Span.Length == 0)
			{
				return false;
			}
			else if (
				Span.Equals("true", StringComparison.OrdinalIgnoreCase) ||
				Span.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
				Span.Equals("on", StringComparison.OrdinalIgnoreCase))
			{
				return true;
			}
			else if (
				Span.Equals("false", StringComparison.OrdinalIgnoreCase) ||
				Span.Equals("no", StringComparison.OrdinalIgnoreCase) ||
				Span.Equals("off", StringComparison.OrdinalIgnoreCase))
			{
				return false;
			}
			else
			{
				int IntValue;
				return int.TryParse(Span, out IntValue) && IntValue != 0;
			}
		}

		/// <summary>
		/// Test to see if the string is a boolean
		/// </summary>
		/// <param name="Value">Boolean to test</param>
		/// <returns>True if the value is true</returns>
		public static bool ToBool(string? Value)
		{
			if (string.IsNullOrEmpty(Value))
			{
				return false;
			}
			else if (string.Compare(Value, "true", true) == 0 ||
				string.Compare(Value, "yes", true) == 0 ||
				string.Compare(Value, "on", true) == 0)
			{
				return true;
			}
			else if (string.Compare(Value, "false", true) == 0 ||
				string.Compare(Value, "no", true) == 0 ||
				string.Compare(Value, "off", true) == 0)
			{
				return false;
			}
			else
			{
				int IntValue;
				return int.TryParse(Value, out IntValue) && IntValue != 0;
			}
		}

		/// <summary>
		/// Test to see if the character is a digit
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsDigit(char C)
		{
			return C >= '0' && C <= '9';
		}

		/// <summary>
		/// Test to see if the character is an alphabet character
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsAlpha(char C)
		{
			return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z');
		}

		/// <summary>
		/// Test to see if the character is a digit or alphabet character
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsAlnum(char C)
		{
			return IsDigit(C) || IsAlpha(C);
		}

		/// <summary>
		/// Test to see if the character is a hex digit (A-F)
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsHexAlphaDigit(char C)
		{
			return (C >= 'A' && C <= 'F') || (C >= 'a' && C <= 'f');
		}

		/// <summary>
		/// Test to see if the character is a hex digit (0-9a-f)
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsHexDigit(char C)
		{
			return IsDigit(C) || IsHexAlphaDigit(C);
		}

		/// <summary>
		/// Test to see if the character is whitespace
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsWhitespace(char C)
		{
			return Char.IsWhiteSpace(C);
		}

		/// <summary>
		/// Test to see if the character is a sign
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsSign(char C)
		{
			return C == '+' || C == '-';
		}

		/// <summary>
		/// Test to see if the character is a hex marker (xX)
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsHexMarker(char C)
		{
			return C == 'x' || C == 'X';
		}

		/// <summary>
		/// Test to see if the character is a float marker (fF)
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsFloatMarker(char C)
		{
			return C == 'f' || C == 'F';
		}

		/// <summary>
		/// Test to see if the character is an exponent marker (eE)
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsExponentMarker(char C)
		{
			return C == 'e' || C == 'E';
		}

		/// <summary>
		/// Test to see if the character is an unsigned marker (uU)
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsUnsignedMarker(char C)
		{
			return C == 'u' || C == 'U';
		}

		/// <summary>
		/// Test to see if the character is a long marker (lL)
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsLongMarker(char C)
		{
			return C == 'l' || C == 'L';
		}

		/// <summary>
		/// Test to see if the span is a numeric value
		/// </summary>
		/// <param name="Span">Span to test</param>
		/// <returns>True if it is</returns>
		public static bool IsNumeric(ReadOnlySpan<char> Span)
		{
			int Index = 0;
			char C = Span[Index];
			if (C == '-' || C == '+')
			{
				++Index;
			}

			bool bHasDot = false;
			for (; Index < Span.Length; ++Index)
			{
				C = Span[Index];
				if (C == '.')
				{
					if (bHasDot)
					{
						return false;
					}
					bHasDot = true;
				}
				else if (!IsDigit(C))
				{
					return false;
				}
			}
			return true;
		}

		/// <summary>
		/// Test to see if the character is a linebreak character
		/// </summary>
		/// <param name="C">Character to test</param>
		/// <returns>True if it is</returns>
		public static bool IsLinebreak(char C)
		{
			return (C >= 0x0a && C <= 0x0d) || C == 0x85 || C == 0x2028 || C == 0x2029;
		}

		/// <summary>
		/// Return an unescaped string
		/// </summary>
		/// <param name="Text">Text to unescape</param>
		/// <returns>Resulting string</returns>
		public static string UnescapeText(string Text)
		{
			StringBuilder Result = new StringBuilder();
			for (int Idx = 0; Idx < Text.Length; Idx++)
			{
				if (Text[Idx] == '\\' && Idx + 1 < Text.Length)
				{
					switch (Text[++Idx])
					{
						case 't':
							Result.Append('\t');
							break;
						case 'r':
							Result.Append('\r');
							break;
						case 'n':
							Result.Append('\n');
							break;
						case '\'':
							Result.Append('\'');
							break;
						case '\"':
							Result.Append('\"');
							break;
						case 'u':
							bool bParsed = false;
							if (Idx + 4 < Text.Length)
							{
								int Value;
								if (int.TryParse(Text.Substring(Idx + 1, 4), System.Globalization.NumberStyles.HexNumber, null, out Value))
								{
									bParsed = true;
									Result.Append((char)Value);
									Idx += 4;
								}
							}
							if (!bParsed)
							{
								Result.Append('\\');
								Result.Append('u');
							}
							break;
						default:
							Result.Append(Text[Idx]);
							break;
					}
				}
				else
				{
					Result.Append(Text[Idx]);
				}
			}
			return Result.ToString();
		}

		/// <summary>
		/// Words that are considered articles when parsing comment strings
		/// Some words are always forced lowercase
		/// </summary>
		private static string[] Articles = new string[]
		{
			"In",
			"As",
			"To",
			"Or",
			"At",
			"On",
			"If",
			"Be",
			"By",
			"The",
			"For",
			"And",
			"With",
			"When",
			"From",
		};

		/// <summary>
		/// Convert a name to a display string
		/// </summary>
		/// <param name="Name">Name to convert</param>
		/// <param name="bIsBool">True if the name is from a boolean property</param>
		/// <returns>Display string</returns>
		public static string NameToDisplayString(StringView Name, bool bIsBool)
		{
			ReadOnlySpan<char> Chars = Name.Span;

			// This is used to indicate that we are in a run of uppercase letter and/or digits.  The code attempts to keep
			// these characters together as breaking them up often looks silly (i.e. "Draw Scale 3 D" as opposed to "Draw Scale 3D"
			bool bInARun = false;
			bool bWasSpace = false;
			bool bWasOpenParen = false;
			bool bWasNumber = false;
			bool bWasMinusSign = false;

			StringBuilder Builder = new StringBuilder();
			for (int CharIndex = 0; CharIndex < Chars.Length; ++CharIndex)
			{
				char ch = Chars[CharIndex];

				bool bLowerCase = Char.IsLower(ch);
				bool bUpperCase = Char.IsUpper(ch);
				bool bIsDigit = Char.IsDigit(ch);
				bool bIsUnderscore = ch == '_';

				// Skip the first character if the property is a bool (they should all start with a lowercase 'b', which we don't want to keep)
				if (CharIndex == 0 && bIsBool && ch == 'b')
				{
					// Check if next character is uppercase as it may be a user created string that doesn't follow the rules of Unreal variables
					if (Chars.Length > 1 && Char.IsUpper(Chars[1]))
					{
						continue;
					}
				}

				// If the current character is upper case or a digit, and the previous character wasn't, then we need to insert a space if there wasn't one previously
				// We don't do this for numerical expressions, for example "-1.2" should not be formatted as "- 1. 2"
				if ((bUpperCase || (bIsDigit && !bWasMinusSign)) && !bInARun && !bWasOpenParen && !bWasNumber)
				{
					if (!bWasSpace && Builder.Length > 0)
					{
						Builder.Append(' ');
						bWasSpace = true;
					}
					bInARun = true;
				}

				// A lower case character will break a run of upper case letters and/or digits
				if (bLowerCase)
				{
					bInARun = false;
				}

				// An underscore denotes a space, so replace it and continue the run
				if (bIsUnderscore)
				{
					ch = ' ';
					bInARun = true;
				}

				// If this is the first character in the string, then it will always be upper-case
				if (Builder.Length == 0)
				{
					ch = Char.ToUpper(ch);
				}
				else if (!bIsDigit && (bWasSpace || bWasOpenParen)) // If this is first character after a space, then make sure it is case-correct
				{

					// Search for a word that needs case repaired
					bool bIsArticle = false;
					foreach (string Article in Articles)
					{
						// Make sure the character following the string we're testing is not lowercase (we don't want to match "in" with "instance")
						int ArticleLength = Article.Length;
						if (CharIndex + ArticleLength < Chars.Length && !Char.IsLower(Chars[CharIndex + ArticleLength]))
						{
							// Does this match the current article?
							if (Chars.Slice(CharIndex, ArticleLength).Equals(Article.AsSpan(), StringComparison.OrdinalIgnoreCase))
							{
								bIsArticle = true;
								break;
							}
						}
					}

					// Start of a keyword, force to lowercase
					if (bIsArticle)
					{
						ch = Char.ToLower(ch);
					}
					else    // First character after a space that's not a reserved keyword, make sure it's uppercase
					{
						ch = Char.ToUpper(ch);
					}
				}

				bWasSpace = ch == ' ';
				bWasOpenParen = ch == '(';

				// What could be included as part of a numerical representation.
				// For example -1.2
				bWasMinusSign = ch == '-';
				bool bPotentialNumericalChar = bWasMinusSign || ch == '.';
				bWasNumber = bIsDigit || (bWasNumber && bPotentialNumericalChar);
				Builder.Append(ch);
			}
			return Builder.ToString();
		}

		/// <summary>
		/// Replace tabs to spaces in a string containing only a single line.
		/// </summary>
		/// <param name="Input">Input string</param>
		/// <param name="TabSpacing">Number of spaces to exchange for tabs</param>
		/// <param name="bEmulateCrBug">Due to a bug in UE ConvertTabsToSpacesInline, any \n is considered part of the line length.</param>
		/// <returns>Resulting string or the original string if the string didn't contain any spaces.</returns>
		public static ReadOnlyMemory<char> TabsToSpaces(ReadOnlyMemory<char> Input, int TabSpacing, bool bEmulateCrBug)
		{
			// If we have any tab characters, then we need to convert them to spaces
			int TabIndex = Input.Span.IndexOf('\t');
			if (TabIndex == -1)
			{
				return Input;
			}
			return TabsToSpacesInternal(Input.Span, TabSpacing, bEmulateCrBug, TabIndex).AsMemory();
		}

		/// <summary>
		/// Replace tabs to spaces in a string containing zero or more lines.
		/// </summary>
		/// <param name="Input">Input string</param>
		/// <param name="TabSpacing">Number of spaces to exchange for tabs</param>
		/// <param name="bEmulateCrBug">Due to a bug in UE ConvertTabsToSpacesInline, any \n is considered part of the line length.</param>
		/// <returns>Resulting string or the original string if the string didn't contain any spaces.</returns>
		public static string TabsToSpaces(String Input, int TabSpacing, bool bEmulateCrBug)
		{
			// If we have any tab characters, then we need to convert them to spaces
			int TabIndex = Input.IndexOf('\t');
			if (TabIndex == -1)
			{
				return Input;
			}
			return TabsToSpacesInternal(Input.AsSpan(), TabSpacing, bEmulateCrBug, TabIndex);
		}

		/// <summary>
		/// Replace tabs to spaces in a string containing zero or more lines.
		/// </summary>
		/// <param name="Span">Input string to convert</param>
		/// <param name="TabSpacing">Number of spaces to exchange for tabs</param>
		/// <param name="bEmulateCrBug">Due to a bug in UE ConvertTabsToSpacesInline, any \n is considered part of the line length.</param>
		/// <param name="TabIndex">Initial tab index</param>
		/// <returns>Resulting string or the original string if the string didn't contain any spaces.</returns>
		private static string TabsToSpacesInternal(ReadOnlySpan<char> Span, int TabSpacing, bool bEmulateCrBug, int TabIndex)
		{
			// Locate the last \n since all tabs have to be computed relative to this.
			int CrPos = Span.Slice(0, TabIndex).LastIndexOf('\n') + 1;

			using (BorrowStringBuilder Borrower = new BorrowStringBuilder(StringBuilderCache.Small))
			{
				StringBuilder Builder = Borrower.StringBuilder;
				int Committed = 0;
				do
				{
					// Commit everything prior to the tab to the builder
					Builder.Append(Span.Slice(0, TabIndex));
					Span = Span.Slice(TabIndex + 1);
					Committed += TabIndex;

					// Add the appropriate number of spaces
					int AdjustedCrPos = CrPos;
					if (bEmulateCrBug && AdjustedCrPos > 0)
					{
						--AdjustedCrPos;
					}
					int SpacesToInsert = TabSpacing - (Committed - AdjustedCrPos) % TabSpacing;
					Builder.AppendSpaces(SpacesToInsert);
					Committed += SpacesToInsert;

					// Search for the next \t or \n
					for (TabIndex = 0; TabIndex < Span.Length; ++TabIndex)
					{
						if (Span[TabIndex] == '\n')
						{
							CrPos = Committed + TabIndex + 1;
						}
						else if (Span[TabIndex] == '\t')
						{
							break;
						}
					}
				} while (TabIndex < Span.Length);

				// Commit the remaining data
				Builder.Append(Span);
				return Builder.ToString();
			}
		}
	}
}
