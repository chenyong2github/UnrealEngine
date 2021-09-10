// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Buffers.Text;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	enum TokenType : byte
	{
		End,
		Identifier,
		Scalar,
		Not,
		Eq,
		Neq,
		LogicalOr,
		LogicalAnd,
		Lt,
		Lte,
		Gt,
		Gte,
		Lparen,
		Rparen,
		Regex
	}

	class TokenReader
	{
		public string Input { get; private set; }
		public int Offset { get; private set; }
		public int Length { get; private set; }

		public StringView Token => new StringView(Input, Offset, Length);
		public TokenType Type { get; private set; }
		public string? Scalar { get; private set; }

		public TokenReader(string Input)
		{
			this.Input = Input;
			MoveNext();
		}

		private void SetCurrent(int Length, TokenType Type, string? Scalar = null)
		{
			this.Length = Length;
			this.Type = Type;
			this.Scalar = Scalar;
		}

		public void MoveNext()
		{
			Offset += Length;

			while (Offset < Input.Length)
			{
				switch (Input[Offset])
				{
					case ' ':
					case '\t':
						Offset++;
						break;
					case '(':
						SetCurrent(1, TokenType.Lparen);
						return;
					case ')':
						SetCurrent(1, TokenType.Rparen);
						return;
					case '!':
						if (Offset + 1 < Input.Length && Input[Offset + 1] == '=')
						{
							SetCurrent(2, TokenType.Neq);
						}
						else
						{
							SetCurrent(1, TokenType.Not);
						}
						return;
					case '&':
						RequireCharacter(Input, Offset + 1, '&');
						SetCurrent(2, TokenType.LogicalAnd);
						return;
					case '|':
						RequireCharacter(Input, Offset + 1, '|');
						SetCurrent(2, TokenType.LogicalOr);
						return;
					case '=':
						RequireCharacter(Input, Offset + 1, '=');
						SetCurrent(2, TokenType.Eq);
						return;
					case '~':
						RequireCharacter(Input, Offset + 1, '=');
						SetCurrent(2, TokenType.Regex);
						return;
					case '>':
						if (Offset + 1 < Input.Length && Input[Offset + 1] == '=')
						{
							SetCurrent(2, TokenType.Gte);
						}
						else
						{
							SetCurrent(1, TokenType.Gt);
						}
						return;
					case '<':
						if (Offset + 1 < Input.Length && Input[Offset + 1] == '=')
						{
							SetCurrent(2, TokenType.Lte);
						}
						else
						{
							SetCurrent(1, TokenType.Lt);
						}
						return;
					case '\"':
					case '\'':
						Utf8String String = ParseString(Input, Offset, out int StringLength);
						SetCurrent(StringLength, TokenType.Scalar, Scalar: String.ToString());
						return;
					case char Char when IsNumber(Char):
						ulong Integer = ParseNumber(Input, Offset, out int IntegerLength);
						SetCurrent(IntegerLength, TokenType.Scalar, Scalar: Integer.ToString(CultureInfo.InvariantCulture));
						return;
					case char Char when IsIdentifier(Char):
						int EndIdx = Offset + 1;
						while (EndIdx < Input.Length && IsIdentifierTail(Input[EndIdx]))
						{
							EndIdx++;
						}
						SetCurrent(EndIdx - Offset, TokenType.Identifier);
						return;
					default:
						throw new ConditionException($"Invalid character at offset {Offset}: '{Input[Offset]}'");
				}
			}

			SetCurrent(0, TokenType.End);
		}

		static string ParseString(string Text, int Idx, out int BytesConsumed)
		{
			char QuoteChar = Text[Idx];
			if (QuoteChar != '\'' && QuoteChar != '\"')
			{
				throw new ConditionException($"Invalid quote character '{(char)QuoteChar}' at offset {Idx}");
			}

			int NumEscapeChars = 0;

			int EndIdx = Idx + 1;
			for (; ; EndIdx++)
			{
				if (EndIdx >= Text.Length)
				{
					throw new ConditionException("Unterminated string in expression");
				}
				else if (Text[EndIdx] == '\\')
				{
					NumEscapeChars++;
					EndIdx++;
				}
				else if (Text[EndIdx] == QuoteChar)
				{
					break;
				}
			}

			char[] Copy = new char[EndIdx - (Idx + 1) - NumEscapeChars];

			int InputIdx = Idx + 1;
			int OutputIdx = 0;
			while (OutputIdx < Copy.Length)
			{
				if (Text[InputIdx] == '\\')
				{
					InputIdx++;
				}
				Copy[OutputIdx++] = Text[InputIdx++];
			}

			BytesConsumed = EndIdx + 1 - Idx;
			return new string(Copy);
		}

		static Dictionary<StringView, ulong> SizeSuffixes = new Dictionary<StringView, ulong>(StringViewComparer.OrdinalIgnoreCase)
		{
			["kb"] = 1024UL,
			["Mb"] = 1024UL * 1024,
			["Gb"] = 1024UL * 1024 * 1024,
			["Tb"] = 1024UL * 1024 * 1024 * 1024,
		};

		static ulong ParseNumber(string Text, int Idx, out int BytesConsumed)
		{
			ulong Value = 0;

			int EndIdx = Idx;
			while (EndIdx < Text.Length && IsNumber(Text[EndIdx]))
			{
				Value = (Value * 10) + (uint)(Text[EndIdx] - '0');
				EndIdx++;
			}

			if (EndIdx < Text.Length && IsIdentifier(Text[EndIdx]))
			{
				int Offset = EndIdx++;
				while (EndIdx < Text.Length && IsIdentifierTail(Text[EndIdx]))
				{
					EndIdx++;
				}

				StringView Suffix = new StringView(Text, Offset, EndIdx - Offset);

				ulong Size;
				if (!SizeSuffixes.TryGetValue(Suffix, out Size))
				{
					throw new ConditionException($"'{Suffix}' is not a valid numerical suffix");
				}
				Value *= Size;
			}

			BytesConsumed = EndIdx - Idx;
			return Value;
		}

		static void RequireCharacter(string Text, int Idx, char Character)
		{
			if (Idx == Text.Length || Text[Idx] != Character)
			{
				throw new ConditionException($"Invalid character at position {Idx}; expected '{Character}'.");
			}
		}

		static bool IsNumber(char Character)
		{
			return Character >= '0' && Character <= '9';
		}

		static bool IsIdentifier(char Character)
		{
			return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || Character == '_';
		}

		static bool IsIdentifierTail(char Character)
		{
			return IsIdentifier(Character) || IsNumber(Character) || Character == '.';
		}
	}

	/// <summary>
	/// Exception thrown when parsing or evaluating a condition
	/// </summary>
	public class ConditionException : Exception
	{
		internal ConditionException(string Text) : base(Text)
		{
		}
	}

	/// <summary>
	/// A conditional expression that can be evaluated against a particular object
	/// </summary>
	class Condition
	{
		[DebuggerDisplay("{Type}")]
		readonly struct Token
		{
			public readonly TokenType Type;
			public readonly byte Index;

			public Token(TokenType Type, int Index)
			{
				this.Type = Type;
				this.Index = (byte)Index;
			}
		}

		List<Token> Tokens = new List<Token>();
		List<string> Strings = new List<string>();

		private Condition(string Text)
		{
			TokenReader Reader = new TokenReader(Text);
			ParseOr(Reader);
			if (Reader.Type != TokenType.End)
			{
				throw new ConditionException($"Unexpected token at offset {Reader.Offset}: {Reader.Token}");
			}
		}

		public static Condition Parse(string Text)
		{
			return new Condition(Text);
		}

		public bool Evaluate(Func<string, IEnumerable<string>> GetPropertyValues)
		{
			int Idx = 0;
			return EvaluateCondition(ref Idx, GetPropertyValues);
		}

		bool EvaluateCondition(ref int Idx, Func<string, IEnumerable<string>> GetPropertyValues)
		{
			bool LhsBool;
			bool RhsBool;
			IEnumerable<string> LhsScalar;
			IEnumerable<string> RhsScalar;

			Token Token = Tokens[Idx++];
			switch (Token.Type)
			{
				case TokenType.Not:
					return !EvaluateCondition(ref Idx, GetPropertyValues);
				case TokenType.Eq:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return LhsScalar.Any(x => RhsScalar.Contains(x, StringComparer.OrdinalIgnoreCase));
				case TokenType.Neq:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return !LhsScalar.Any(x => RhsScalar.Contains(x, StringComparer.OrdinalIgnoreCase));
				case TokenType.LogicalOr:
					LhsBool = EvaluateCondition(ref Idx, GetPropertyValues);
					RhsBool = EvaluateCondition(ref Idx, GetPropertyValues);
					return LhsBool || RhsBool;
				case TokenType.LogicalAnd:
					LhsBool = EvaluateCondition(ref Idx, GetPropertyValues);
					RhsBool = EvaluateCondition(ref Idx, GetPropertyValues);
					return LhsBool && RhsBool;
				case TokenType.Lt:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return AsIntegers(LhsScalar).Any(x => AsIntegers(RhsScalar).Any(y => x < y));
				case TokenType.Lte:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return AsIntegers(LhsScalar).Any(x => AsIntegers(RhsScalar).Any(y => x <= y));
				case TokenType.Gt:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return AsIntegers(LhsScalar).Any(x => AsIntegers(RhsScalar).Any(y => x > y));
				case TokenType.Gte:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return AsIntegers(LhsScalar).Any(x => AsIntegers(RhsScalar).Any(y => x >= y));
				case TokenType.Regex:
					LhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					RhsScalar = EvaluateScalar(ref Idx, GetPropertyValues);
					return LhsScalar.Any(x => RhsScalar.Any(y => Regex.IsMatch(x, y, RegexOptions.IgnoreCase)));
				default:
					throw new ConditionException("Invalid token type");
			}
		}

		IEnumerable<string> EvaluateScalar(ref int Idx, Func<string, IEnumerable<string>> GetPropertyValues)
		{
			Token Token = Tokens[Idx++];
			switch (Token.Type)
			{
				case TokenType.Identifier:
					return GetPropertyValues(Strings[Token.Index]);
				case TokenType.Scalar:
					return new string[] { Strings[Token.Index] };
				default:
					throw new ConditionException("Invalid token type");
			}
		}

		static IEnumerable<long> AsIntegers(IEnumerable<string> Scalars)
		{
			foreach (string Scalar in Scalars)
			{
				long Value;
				if (long.TryParse(Scalar, out Value))
				{
					yield return Value;
				}
			}
		}

		void ParseOr(TokenReader Reader)
		{
			int StartCount = Tokens.Count;
			ParseAnd(Reader);

			if (Reader.Type == TokenType.LogicalOr)
			{
				Tokens.Insert(StartCount, new Token(TokenType.LogicalOr, 0));
				Reader.MoveNext();
				ParseOr(Reader);
			}
		}

		void ParseAnd(TokenReader Reader)
		{
			int StartCount = Tokens.Count;
			ParseBoolean(Reader);

			if (Reader.Type == TokenType.LogicalAnd)
			{
				Tokens.Insert(StartCount, new Token(TokenType.LogicalAnd, 0));
				Reader.MoveNext();
				ParseAnd(Reader);
			}
		}

		void ParseBoolean(TokenReader Reader)
		{
			while (Reader.Type == TokenType.Not)
			{
				Tokens.Add(new Token(Reader.Type, 0));
				Reader.MoveNext();
			}

			if (Reader.Type == TokenType.Lparen)
			{
				Reader.MoveNext();
				ParseOr(Reader);
				if (Reader.Type != TokenType.Rparen)
				{
					throw new ConditionException($"Missing ')' at offset {Reader.Offset}");
				}
				Reader.MoveNext();
			}
			else
			{
				ParseComparison(Reader);
			}
		}

		void ParseComparison(TokenReader Reader)
		{
			int StartCount = Tokens.Count;
			ParseScalar(Reader);

			if (Reader.Type == TokenType.Lt 
				|| Reader.Type == TokenType.Lte 
				|| Reader.Type == TokenType.Gt 
				|| Reader.Type == TokenType.Gte 
				|| Reader.Type == TokenType.Eq 
				|| Reader.Type == TokenType.Neq
				|| Reader.Type == TokenType.Regex)
			{
				Tokens.Insert(StartCount, new Token(Reader.Type, 0));
				Reader.MoveNext();
				ParseScalar(Reader);
			}
		}

		void ParseScalar(TokenReader Reader)
		{
			if (Reader.Type == TokenType.Identifier)
			{
				Strings.Add(Reader.Token.ToString());
				Tokens.Add(new Token(TokenType.Identifier, Strings.Count - 1));
				Reader.MoveNext();
			}
			else if (Reader.Type == TokenType.Scalar)
			{
				Strings.Add(Reader.Scalar!);
				Tokens.Add(new Token(TokenType.Scalar, Strings.Count - 1));
				Reader.MoveNext();
			}
			else
			{
				throw new ConditionException($"Unexpected token '{Reader.Token}' at offset {Reader.Offset}");
			}
		}
	}
}
