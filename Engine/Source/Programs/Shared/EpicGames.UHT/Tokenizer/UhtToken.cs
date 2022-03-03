// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace EpicGames.UHT.Tokenizer
{
	public enum UhtTokenType
	{
		EndOfFile,          // End of file token.
		EndOfDefault,       // End of default value.
		EndOfType,          // End of type
		EndOfDeclaration,	// End of declaration
		Line,				// Line of text (when calling GetLine only)
		Identifier,			// Alphanumeric identifier.
		Symbol,				// Symbol.
		TrueConst,			// True value via identifier
		FalseConst,			// False value via identifier
		FloatConst,			// Floating point constant
		DecimalConst,		// Decimal Integer constant
		HexConst,			// Hex integer constant
		CharConst,			// Single character constant
		StringConst,		// String constant
	}

	public static class UhtTokenTypeExtensions
	{
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

	public struct UhtToken
	{
		public const int MaxNameLength = 1024;
		public const int MaxStringLength = 1024;

		public UhtTokenType TokenType { get; set; }
		public int UngetPos { get; set; }
		public int UngetLine { get; set; }
		public int InputStartPos { get; set; }
		public int InputEndPos { get => this.InputStartPos + this.Value.Span.Length; }
		public int InputLine { get; set; }
		public StringView Value { get; set; }

		public UhtToken(UhtTokenType TokenType)
		{
			this.TokenType = TokenType;
			this.UngetPos = 0;
			this.UngetLine = 0;
			this.InputStartPos = 0;
			this.InputLine = 0;
			this.Value = new StringView();
		}

		public UhtToken(UhtTokenType TokenType, int UngetPos, int UngetLine, int InputStartPos, int InputLine, StringView Value)
		{
			this.TokenType = TokenType;
			this.UngetPos = UngetPos;
			this.UngetLine = UngetLine;
			this.InputStartPos = InputStartPos;
			this.InputLine = InputLine;
			this.Value = Value;
		}

		public static implicit operator bool(UhtToken Token)
		{
			return !Token.IsEndType();
		}

		public bool IsEndType()
		{
			return this.TokenType.IsEndType();
		}

		public bool IsValue(char Value)
		{
			return this.Value.Span.Length == 1 && this.Value.Span[0] == Value;
		}

		public bool IsValue(string Value, bool bIgnoreCase = false)
		{
			return this.Value.Span.Equals(Value, bIgnoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
		}

		public bool IsValue(StringView Value, bool bIgnoreCase = false)
		{
			return this.Value.Span.Equals(Value.Span, bIgnoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
		}

		public bool ValueStartsWith(string Value, bool bIgnoreCase = false)
		{
			return this.Value.Span.StartsWith(Value, bIgnoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
		}

		public bool IsIdentifier()
		{
			return this.TokenType == UhtTokenType.Identifier;
		}

		public bool IsIdentifier(string Identifier, bool bIgnoreCase = false)
		{
			return IsIdentifier() && IsValue(Identifier, bIgnoreCase);
		}

		public bool IsIdentifier(StringView Identifier, bool bIgnoreCase = false)
		{
			return IsIdentifier() && IsValue(Identifier, bIgnoreCase);
		}

		public bool IsSymbol()
		{
			return this.TokenType == UhtTokenType.Symbol;
		}

		public bool IsSymbol(char Symbol)
		{
			return IsSymbol() && IsValue(Symbol);
		}

		public bool IsSymbol(string Symbol)
		{
			return IsSymbol() && IsValue(Symbol);
		}

		public bool IsSymbol(StringView Symbol)
		{
			return IsSymbol() && IsValue(Symbol);
		}

		public bool IsConstInt()
		{
			return this.TokenType == UhtTokenType.DecimalConst || this.TokenType == UhtTokenType.HexConst;
		}

		public bool IsConstFloat()
		{
			return this.TokenType == UhtTokenType.FloatConst;
		}

		// Get the integer value of the token.  Only supported for decimal, hexidecimal, and floating point values
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

		// Get the integer value of the token.  Only supported for decimal, hexadecimal, and floating point values
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

		// Get the float value of the token.  Only supported for decimal, hexadecimal, and floating point values
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

		// Get the double value of the token.  Only supported for decimal, hexadecimal, and floating point values
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

		public bool IsConstString()
		{
			return this.TokenType == UhtTokenType.StringConst || this.TokenType == UhtTokenType.CharConst;
		}

		// Return the token value for string constants
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

		public static string Join(IEnumerable<UhtToken> Tokens)
		{
			StringBuilder Builder = new StringBuilder();
			foreach (var Token in Tokens)
			{
				Builder.Append(Token.Value.ToString());
			}
			return Builder.ToString();
		}

		public static string Join(char Seperator, IEnumerable<UhtToken> Tokens)
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
					Builder.Append(Seperator);
				}
				Builder.Append(Token.Value.ToString());
			}
			return Builder.ToString();
		}

		public static string Join(string Seperator, IEnumerable<UhtToken> Tokens)
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
					Builder.Append(Seperator);
				}
				Builder.Append(Token.Value.ToString());
			}
			return Builder.ToString();
		}

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
	}
}
