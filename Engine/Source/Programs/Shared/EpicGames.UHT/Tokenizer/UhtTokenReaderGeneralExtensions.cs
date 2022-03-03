// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;

namespace EpicGames.UHT.Tokenizer
{
	public static class UhtTokenReaderGeneralExtensions
	{
		public static bool TryOptional(this IUhtTokenReader TokenReader, string Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier(Text) || Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
				return true;
			}
			return false;
		}

		public static bool TryOptional(this IUhtTokenReader TokenReader, StringView Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier(Text) || Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
				return true;
			}
			return false;
		}

		public static bool TryPeekOptional(this IUhtTokenReader TokenReader, string Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			return Token.IsIdentifier(Text) || Token.IsSymbol(Text);
		}

		/// <summary>
		/// Test to see if the next token is one of the given strings.
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">List of keywords to test</param>
		/// <returns>Index of matched string or -1 if nothing matched</returns>
		public static int TryOptional(this IUhtTokenReader TokenReader, string[] Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			for (int Index = 0, EndIndex = Text.Length; Index < EndIndex; ++Index)
			{
				if (Token.IsIdentifier(Text[Index]) || Token.IsSymbol(Text[Index]))
				{
					TokenReader.ConsumeToken();
					return Index;
				}
			}
			return -1;
		}

		public static bool TryOptional(this IUhtTokenReader TokenReader, char Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
				return true;
			}
			return false;
		}

		public static bool TryOptional(this IUhtTokenReader TokenReader, char Text, out UhtToken OutToken)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsSymbol(Text))
			{
				OutToken = Token;
				TokenReader.ConsumeToken();
				return true;
			}
			OutToken = new UhtToken();
			return false;
		}

		public static bool TryPeekOptional(this IUhtTokenReader TokenReader, char Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			return Token.IsSymbol(Text);
		}

		public static IUhtTokenReader Optional(this IUhtTokenReader TokenReader, string Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier(Text) || Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}

		public static IUhtTokenReader Optional(this IUhtTokenReader TokenReader, string Text, Action Action)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier(Text) || Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
				Action();
			}
			return TokenReader;
		}

		public static IUhtTokenReader Optional(this IUhtTokenReader TokenReader, string Text, Action YesAction, Action NoAction)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier(Text) || Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
				YesAction();
			}
			else
			{
				NoAction();
			}
			return TokenReader;
		}

		public static IUhtTokenReader Optional(this IUhtTokenReader TokenReader, char Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}

		public static IUhtTokenReader Optional(this IUhtTokenReader TokenReader, char Text, Action Action)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
				Action();
			}
			return TokenReader;
		}

		public static IUhtTokenReader OptionalStartsWith(this IUhtTokenReader TokenReader, string Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier() && Token.ValueStartsWith(Text))
			{
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}

		public static IUhtTokenReader OptionalStartsWith(this IUhtTokenReader TokenReader, string Text, UhtTokenDelegate TokenDelegate)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier() && Token.ValueStartsWith(Text))
			{
				UhtToken CurrentToken = Token;
				TokenReader.ConsumeToken();
				TokenDelegate(ref CurrentToken);
			}
			return TokenReader;
		}

		public static IUhtTokenReader Require(this IUhtTokenReader TokenReader, string Text, object? ExceptionContext = null)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier(Text) || Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
			}
			else
			{
				throw new UhtTokenException(TokenReader, Token, Text, ExceptionContext);
			}
			return TokenReader;
		}

		public static IUhtTokenReader Require(this IUhtTokenReader TokenReader, string Text, UhtTokenDelegate TokenDelegate)
		{
			return TokenReader.Require(Text, null, TokenDelegate);
		}

		public static IUhtTokenReader Require(this IUhtTokenReader TokenReader, string Text, object? ExceptionContext, UhtTokenDelegate TokenDelegate)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier(Text) || Token.IsSymbol(Text))
			{
				UhtToken CurrentToken = Token;
				TokenReader.ConsumeToken();
				TokenDelegate(ref CurrentToken);
			}
			else
			{
				throw new UhtTokenException(TokenReader, Token, Text, ExceptionContext);
			}
			return TokenReader;
		}

		public static IUhtTokenReader Require(this IUhtTokenReader TokenReader, char Text, object? ExceptionContext = null)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
			}
			else
			{
				throw new UhtTokenException(TokenReader, Token, Text, ExceptionContext);
			}
			return TokenReader;
		}

		public static IUhtTokenReader Require(this IUhtTokenReader TokenReader, char Text, UhtTokenDelegate TokenDelegate)
		{
			return TokenReader.Require(Text, null, TokenDelegate);
		}

		public static IUhtTokenReader Require(this IUhtTokenReader TokenReader, char Text, object? ExceptionContext, UhtTokenDelegate TokenDelegate)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsSymbol(Text))
			{
				UhtToken CurrentToken = Token;
				TokenReader.ConsumeToken();
				TokenDelegate(ref CurrentToken);
			}
			else
			{
				throw new UhtTokenException(TokenReader, Token, Text, ExceptionContext);
			}
			return TokenReader;
		}
	}
}
