// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace EpicGames.UHT.Tokenizer
{

	/// <summary>
	/// Type of the token
	/// </summary>
	public enum UhtTokenType
	{

		/// <summary>
		/// End of file token.
		/// </summary>
		EndOfFile,

		/// <summary>
		/// End of default value. 
		/// </summary>
		EndOfDefault,

		/// <summary>
		/// End of type
		/// </summary>
		EndOfType,

		/// <summary>
		/// End of declaration
		/// </summary>
		EndOfDeclaration,

		/// <summary>
		/// Line of text (when calling GetLine only)
		/// </summary>
		Line,

		/// <summary>
		/// Alphanumeric identifier.
		/// </summary>
		Identifier,

		/// <summary>
		/// Symbol.
		/// </summary>
		Symbol,

		/// <summary>
		/// Floating point constant
		/// </summary>
		FloatConst,

		/// <summary>
		/// Decimal Integer constant
		/// </summary>
		DecimalConst,

		/// <summary>
		/// Hex integer constant
		/// </summary>
		HexConst,

		/// <summary>
		/// Single character constant
		/// </summary>
		CharConst,

		/// <summary>
		/// String constant
		/// </summary>
		StringConst,
	}

	/// <summary>
	/// Series of extension methods for the token type
	/// </summary>
	public static class UhtTokenTypeExtensions
	{

		/// <summary>
		/// Return true if the token type is an end type
		/// </summary>
		/// <param name="TokenType">Token type in question</param>
		/// <returns>True if the token type is an end type</returns>
		public static bool IsEndType(this UhtTokenType TokenType)
		{
			switch (TokenType)
			{
				case UhtTokenType.EndOfFile:
				case UhtTokenType.EndOfDefault:
				case UhtTokenType.EndOfType:
				case UhtTokenType.EndOfDeclaration:
					return true;
				default:
					return false;
			}
		}
	}

	/// <summary>
	/// Token declaration
	/// </summary>
	public struct UhtToken
	{

		/// <summary>
		/// Names/Identifiers can not be longer that the following
		/// </summary>
		public const int MaxNameLength = 1024;

		/// <summary>
		/// Strings can not be longer than the following.
		/// </summary>
		public const int MaxStringLength = 1024;

		/// <summary>
		/// Type of the token
		/// </summary>
		public UhtTokenType TokenType { get; set; }

		/// <summary>
		/// Position to restore the reader
		/// </summary>
		public int UngetPos { get; set; }

		/// <summary>
		/// Line to restore the reader
		/// </summary>
		public int UngetLine { get; set; }

		/// <summary>
		/// Starting position of the token value
		/// </summary>
		public int InputStartPos { get; set; }

		/// <summary>
		/// End position of the token value
		/// </summary>
		public int InputEndPos { get => this.InputStartPos + this.Value.Span.Length; }

		/// <summary>
		/// Line containing the token
		/// </summary>
		public int InputLine { get; set; }

		/// <summary>
		/// Token value
		/// </summary>
		public StringView Value { get; set; }

		/// <summary>
		/// Construct a new token
		/// </summary>
		/// <param name="TokenType">Type of the token</param>
		public UhtToken(UhtTokenType TokenType)
		{
			this.TokenType = TokenType;
			this.UngetPos = 0;
			this.UngetLine = 0;
			this.InputStartPos = 0;
			this.InputLine = 0;
			this.Value = new StringView();
		}

		/// <summary>
		/// Construct a new token
		/// </summary>
		/// <param name="TokenType">Type of token</param>
		/// <param name="UngetPos">Unget position</param>
		/// <param name="UngetLine">Unget line</param>
		/// <param name="InputStartPos">Start position of value</param>
		/// <param name="InputLine">Line of value</param>
		/// <param name="Value">Token value</param>
		public UhtToken(UhtTokenType TokenType, int UngetPos, int UngetLine, int InputStartPos, int InputLine, StringView Value)
		{
			this.TokenType = TokenType;
			this.UngetPos = UngetPos;
			this.UngetLine = UngetLine;
			this.InputStartPos = InputStartPos;
			this.InputLine = InputLine;
			this.Value = Value;
		}

		/// <summary>
		/// True if the token isn't an end token
		/// </summary>
		/// <param name="Token">Token in question</param>
		public static implicit operator bool(UhtToken Token)
		{
			return !Token.IsEndType();
		}

		/// <summary>
		/// Return true if the token is an end token
		/// </summary>
		/// <returns>True if the token is an end token</returns>
		public bool IsEndType()
		{
			return this.TokenType.IsEndType();
		}

		/// <summary>
		/// Test to see if the value matches the given character
		/// </summary>
		/// <param name="Value">Value to test</param>
		/// <returns>True if the token value matches the given value</returns>
		public bool IsValue(char Value)
		{
			return this.Value.Span.Length == 1 && this.Value.Span[0] == Value;
		}

		/// <summary>
		/// Test to see if the value matches the given string
		/// </summary>
		/// <param name="Value">Value to test</param>
		/// <param name="bIgnoreCase">If true, ignore case</param>
		/// <returns>True if the value matches</returns>
		public bool IsValue(string Value, bool bIgnoreCase = false)
		{
			return this.Value.Span.Equals(Value, bIgnoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
		}

		/// <summary>
		/// Test to see if the value matches the given string
		/// </summary>
		/// <param name="Value">Value to test</param>
		/// <param name="bIgnoreCase">If true, ignore case</param>
		/// <returns>True if the value matches</returns>
		public bool IsValue(StringView Value, bool bIgnoreCase = false)
		{
			return this.Value.Span.Equals(Value.Span, bIgnoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
		}

		/// <summary>
		/// Test to see if the value starts with the given string
		/// </summary>
		/// <param name="Value">Value to test</param>
		/// <param name="bIgnoreCase">If true, ignore case</param>
		/// <returns>True is the value starts with the given string</returns>
		public bool ValueStartsWith(string Value, bool bIgnoreCase = false)
		{
			return this.Value.Span.StartsWith(Value, bIgnoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
		}

		/// <summary>
		/// Return true if the token is an identifier
		/// </summary>
		/// <returns>True if the token is an identifier</returns>
		public bool IsIdentifier()
		{
			return this.TokenType == UhtTokenType.Identifier;
		}

		/// <summary>
		/// Return true if the identifier matches
		/// </summary>
		/// <param name="Identifier">Identifier to test</param>
		/// <param name="bIgnoreCase">If true, ignore case</param>
		/// <returns>True if the identifier matches</returns>
		public bool IsIdentifier(string Identifier, bool bIgnoreCase = false)
		{
			return IsIdentifier() && IsValue(Identifier, bIgnoreCase);
		}

		/// <summary>
		/// Return true if the identifier matches
		/// </summary>
		/// <param name="Identifier">Identifier to test</param>
		/// <param name="bIgnoreCase">If true, ignore case</param>
		/// <returns>True if the identifier matches</returns>
		public bool IsIdentifier(StringView Identifier, bool bIgnoreCase = false)
		{
			return IsIdentifier() && IsValue(Identifier, bIgnoreCase);
		}

		/// <summary>
		/// Return true if the token is a symbol
		/// </summary>
		/// <returns>True if the token is a symbol</returns>
		public bool IsSymbol()
		{
			return this.TokenType == UhtTokenType.Symbol;
		}

		/// <summary>
		/// Return true if the symbol matches
		/// </summary>
		/// <param name="Symbol">Symbol to test</param>
		/// <returns>True if the symbol matches</returns>
		public bool IsSymbol(char Symbol)
		{
			return IsSymbol() && IsValue(Symbol);
		}

		/// <summary>
		/// Return true if the symbol matches
		/// </summary>
		/// <param name="Symbol">Symbol to test</param>
		/// <returns>True if the symbol matches</returns>
		public bool IsSymbol(string Symbol)
		{
			return IsSymbol() && IsValue(Symbol);
		}

		/// <summary>
		/// Return true if the symbol matches
		/// </summary>
		/// <param name="Symbol">Symbol to test</param>
		/// <returns>True if the symbol matches</returns>
		public bool IsSymbol(StringView Symbol)
		{
			return IsSymbol() && IsValue(Symbol);
		}

		/// <summary>
		/// Return true if the token is a constant integer
		/// </summary>
		/// <returns>True if constant integer</returns>
		public bool IsConstInt()
		{
			return this.TokenType == UhtTokenType.DecimalConst || this.TokenType == UhtTokenType.HexConst;
		}

		/// <summary>
		/// Return true if the token is a constant floag
		/// </summary>
		/// <returns>True if constant float</returns>
		public bool IsConstFloat()
		{
			return this.TokenType == UhtTokenType.FloatConst;
		}

		/// <summary>
		/// Get the integer value of the token.  Only supported for decimal, hexadecimal, and floating point values
		/// </summary>
		/// <param name="Value">Resulting value</param>
		/// <returns>True if the value was set</returns>
		public bool GetConstInt(out int Value)
		{
			switch (TokenType)
			{
				case UhtTokenType.DecimalConst:
					Value = (int)GetDecimalValue();
					return true;
				case UhtTokenType.HexConst:
					Value = (int)GetHexValue();
					return true;
				case UhtTokenType.FloatConst:
					{
						float Float = GetFloatValue();
						Value = (int)Float;
						return Float == Value;
					}
				default:
					Value = 0;
					return false;
			}
		}

		/// <summary>
		/// Get the integer value of the token.  Only supported for decimal, hexadecimal, and floating point values
		/// </summary>
		/// <param name="Value">Resulting value</param>
		/// <returns>True if the value was set</returns>
		public bool GetConstLong(out long Value)
		{
			switch (TokenType)
			{
				case UhtTokenType.DecimalConst:
					Value = GetDecimalValue();
					return true;
				case UhtTokenType.HexConst:
					Value = GetHexValue();
					return true;
				case UhtTokenType.FloatConst:
					{
						float Float = GetFloatValue();
						Value = (long)Float;
						return Float == Value;
					}
				default:
					Value = 0;
					return false;
			}
		}

		/// <summary>
		/// Get the float value of the token.  Only supported for decimal, hexadecimal, and floating point values
		/// </summary>
		/// <param name="Value">Resulting value</param>
		/// <returns>True if the value was set</returns>
		public bool GetConstFloat(out float Value)
		{
			switch (TokenType)
			{
				case UhtTokenType.DecimalConst:
					Value = (float)GetDecimalValue();
					return true;
				case UhtTokenType.HexConst:
					Value = (float)GetHexValue();
					return true;
				case UhtTokenType.FloatConst:
					Value = GetFloatValue();
					return true;
				default:
					Value = 0;
					return false;
			}
		}

		/// <summary>
		/// Get the double value of the token.  Only supported for decimal, hexadecimal, and floating point values
		/// </summary>
		/// <param name="Value">Resulting value</param>
		/// <returns>True if the value was set</returns>
		public bool GetConstDouble(out double Value)
		{
			switch (TokenType)
			{
				case UhtTokenType.DecimalConst:
					Value = (float)GetDecimalValue();
					return true;
				case UhtTokenType.HexConst:
					Value = (float)GetHexValue();
					return true;
				case UhtTokenType.FloatConst:
					Value = GetDoubleValue();
					return true;
				default:
					Value = 0;
					return false;
			}
		}

		/// <summary>
		/// Return true if the token is a constant string (or a char constant)
		/// </summary>
		/// <returns>True if the token is a string or character constant</returns>
		public bool IsConstString()
		{
			return this.TokenType == UhtTokenType.StringConst || this.TokenType == UhtTokenType.CharConst;
		}

		// Return the token value for string constants

		/// <summary>
		/// Return an un-escaped string.  The surrounding quotes will be removed and escaped characters will be converted to the actual values.
		/// </summary>
		/// <param name="MessageSite"></param>
		/// <returns>Resulting string</returns>
		/// <exception cref="UhtException">Thrown if the token type is not a string or character constant</exception>
		public StringView GetUnescapedString(IUhtMessageSite MessageSite)
		{
			switch (this.TokenType)
			{
				case UhtTokenType.StringConst:
					ReadOnlySpan<char> Span = this.Value.Span.Slice(1, this.Value.Span.Length - 2);
					int Index = Span.IndexOf('\\');
					if (Index == -1)
					{
						return new StringView(this.Value, 1, Span.Length);
					}
					else
					{
						StringBuilder Builder = new StringBuilder();
						while (Index >= 0)
						{
							Builder.Append(Span.Slice(0, Index));
							if (Span[Index + 1] == 'n')
							{
								Builder.Append('\n');
							}
							Span = Span.Slice(Index + 1);
							Index = Span.IndexOf('\\');
						}
						Builder.Append(Span);
						return new StringView(Builder.ToString());
					}

				case UhtTokenType.CharConst:
					if (this.Value.Span[1] == '\\')
					{
						switch (this.Value.Span[2])
						{
							case 't':
								return new StringView("\t");
							case 'n':
								return new StringView("\n");
							case 'r':
								return new StringView("\r");
							default:
								return new StringView(this.Value, 2, 1);
						}
					}
					else
					{
						return new StringView(this.Value, 1, 1);
					}
			}

			throw new UhtException(MessageSite, this.InputLine, "Call to GetUnescapedString on token that isn't a string or char constant");
		}

		/// <summary>
		/// Return a string representation of the token value.  This will convert numeric values and format them.
		/// </summary>
		/// <param name="bRespectQuotes">If true, embedded quotes will be respected</param>
		/// <returns>Resulting string</returns>
		public StringView GetConstantValue(bool bRespectQuotes = false)
		{
			switch (this.TokenType)
			{
				case UhtTokenType.DecimalConst:
					return new StringView(GetDecimalValue().ToString(NumberFormatInfo.InvariantInfo));
				case UhtTokenType.HexConst:
					return new StringView(GetHexValue().ToString(NumberFormatInfo.InvariantInfo));
				case UhtTokenType.FloatConst:
					return new StringView(GetFloatValue().ToString("F6", NumberFormatInfo.InvariantInfo));
				case UhtTokenType.CharConst:
				case UhtTokenType.StringConst:
					return GetTokenString(bRespectQuotes);
				default:
					return "NotConstant";
			}
		}

		/// <summary>
		/// Return an un-escaped string.  The surrounding quotes will be removed and escaped characters will be converted to the actual values.
		/// </summary>
		/// <param name="bRespectQuotes">If true, respect embedded quotes</param>
		/// <returns>Resulting string</returns>
		public StringView GetTokenString(bool bRespectQuotes = false)
		{
			StringViewBuilder SVB = new StringViewBuilder();
			switch (this.TokenType)
			{
				case UhtTokenType.StringConst:
					StringView SubValue = new StringView(this.Value, 1, this.Value.Span.Length - 2);
					while (SubValue.Span.Length > 0)
					{
						int SlashIndex = SubValue.Span.IndexOf('\\');
						if (SlashIndex == -1)
						{
							SVB.Append(SubValue);
							break;
						}
						if (SlashIndex > 0)
						{
							SVB.Append(new StringView(SubValue, 0, SlashIndex));
						}
						if (SlashIndex + 1 == SubValue.Span.Length)
						{
							break;
						}

						if (SlashIndex + 1 < SubValue.Span.Length)
						{
							char c = SubValue.Span[SlashIndex + 1];
							if (c == 'n')
							{
								c = '\n';
							}
							else if (bRespectQuotes && c == '"')
							{
								SVB.Append('\\');
							}
							SVB.Append(c);
							SubValue = new StringView(SubValue, SlashIndex + 2);
						}
					}
					break;

				case UhtTokenType.CharConst:
					char CharConst = this.Value.Span[1];
					if (CharConst == '\\')
					{
						CharConst = this.Value.Span[2];
						switch (CharConst)
						{
							case 't':
								CharConst = '\t';
								break;
							case 'n':
								CharConst = '\n';
								break;
							case 'r':
								CharConst = '\r';
								break;
						}
					}
					SVB.Append(CharConst);
					break;

				default:
					throw new UhtIceException("Call to GetTokenString on a token that isn't a string or char constant");
			}
			return SVB.ToStringView();
		}

		/// <summary>
		/// Join the given tokens into a string
		/// </summary>
		/// <param name="Tokens">Tokens to join</param>
		/// <returns>Joined strings</returns>
		public static string Join(IEnumerable<UhtToken> Tokens)
		{
			StringBuilder Builder = new StringBuilder();
			foreach (var Token in Tokens)
			{
				Builder.Append(Token.Value.ToString());
			}
			return Builder.ToString();
		}

		/// <summary>
		/// Join the given tokens into a string
		/// </summary>
		/// <param name="Separator">Separator between tokens</param>
		/// <param name="Tokens">Tokens to join</param>
		/// <returns>Joined strings</returns>
		public static string Join(char Separator, IEnumerable<UhtToken> Tokens)
		{
			StringBuilder Builder = new StringBuilder();
			bool bIncludeSeperator = false;
			foreach (var Token in Tokens)
			{
				if (!bIncludeSeperator)
				{
					bIncludeSeperator = true;
				}
				else
				{
					Builder.Append(Separator);
				}
				Builder.Append(Token.Value.ToString());
			}
			return Builder.ToString();
		}

		/// <summary>
		/// Join the given tokens into a string
		/// </summary>
		/// <param name="Separator">Separator between tokens</param>
		/// <param name="Tokens">Tokens to join</param>
		/// <returns>Joined strings</returns>
		public static string Join(string Separator, IEnumerable<UhtToken> Tokens)
		{
			StringBuilder Builder = new StringBuilder();
			bool bIncludeSeperator = false;
			foreach (var Token in Tokens)
			{
				if (!bIncludeSeperator)
				{
					bIncludeSeperator = true;
				}
				else
				{
					Builder.Append(Separator);
				}
				Builder.Append(Token.Value.ToString());
			}
			return Builder.ToString();
		}

		/// <summary>
		/// Convert the token to a string.  This will be the value.
		/// </summary>
		/// <returns>Value of the token</returns>
		public override string ToString()
		{
			if (IsEndType())
			{
				return "<none>";
			}
			else
			{
				return Value.Span.ToString();
			}
		}

		float GetFloatValue()
		{
			if (Value.Span.Length > 0)
			{
				if (UhtFCString.IsFloatMarker(Value.Span[Value.Span.Length - 1]))
				{
					return float.Parse(Value.Span.Slice(0, Value.Span.Length - 1));
				}
			}
			return float.Parse(Value.Span);
		}

		double GetDoubleValue()
		{
			if (Value.Span.Length > 0)
			{
				if (UhtFCString.IsFloatMarker(Value.Span[Value.Span.Length - 1]))
				{
					return double.Parse(Value.Span.Slice(0, Value.Span.Length - 1));
				}
			}
			return double.Parse(Value.Span);
		}

		long GetDecimalValue()
		{
			ReadOnlySpan<char> Span = this.Value.Span;
			bool bIsUnsigned = false;
			while (Span.Length > 0)
			{
				char C = Span[Span.Length - 1];
				if (UhtFCString.IsLongMarker(C))
				{
					Span = Span.Slice(0, Span.Length - 1);
				}
				else if (UhtFCString.IsUnsignedMarker(C))
				{
					bIsUnsigned = true;
					Span = Span.Slice(0, Span.Length - 1);
				}
				else
				{
					break;
				}
			}
			return bIsUnsigned ? (long)Convert.ToUInt64(Span.ToString(), 10) : Convert.ToInt64(Span.ToString(), 10);
		}

		long GetHexValue()
		{
			return Convert.ToInt64(this.Value.ToString(), 16);
		}
	}
}
