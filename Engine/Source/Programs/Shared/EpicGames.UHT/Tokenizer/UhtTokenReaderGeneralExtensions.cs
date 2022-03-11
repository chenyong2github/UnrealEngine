// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;

namespace EpicGames.UHT.Tokenizer
{
	
	/// <summary>
	/// Collection of general token reader extensions
	/// </summary>
	public static class UhtTokenReaderGeneralExtensions
	{

		/// <summary>
		/// Try to parse the given text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <returns>True if the text matched</returns>
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

		/// <summary>
		/// Try to parse the given text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <returns>True if the text matched</returns>
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

		/// <summary>
		/// Try to parse the given text.  However, the matching token will not be consumed.
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <returns>True if the text matched</returns>
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

		/// <summary>
		/// Try to parse the given text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <returns>True if the text matched</returns>
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

		/// <summary>
		/// Try to parse the given text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <param name="OutToken">Open that was matched</param>
		/// <returns>True if the text matched</returns>
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

		/// <summary>
		/// Try to parse the given text.  However, the matching token will not be consumed.
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <returns>True if the text matched</returns>
		public static bool TryPeekOptional(this IUhtTokenReader TokenReader, char Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			return Token.IsSymbol(Text);
		}

		/// <summary>
		/// Parse optional text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader Optional(this IUhtTokenReader TokenReader, string Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier(Text) || Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}

		/// <summary>
		/// Parse optional text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <param name="Action">Action to invoke if the text was found</param>
		/// <returns>Token reader</returns>
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

		/// <summary>
		/// Parse optional text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader Optional(this IUhtTokenReader TokenReader, char Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsSymbol(Text))
			{
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}

		/// <summary>
		/// Parse optional text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <param name="Action">Action to invoke if the text was found</param>
		/// <returns>Token reader</returns>
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

		/// <summary>
		/// Parse optional token that starts with the given text 
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader OptionalStartsWith(this IUhtTokenReader TokenReader, string Text)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier() && Token.ValueStartsWith(Text))
			{
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}

		/// <summary>
		/// Parse optional token that starts with the given text 
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Text to match</param>
		/// <param name="TokenDelegate">Delegate to invoke on a match</param>
		/// <returns>Token reader</returns>
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

		/// <summary>
		/// Require the given text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Required text</param>
		/// <param name="ExceptionContext">Extra exception context</param>
		/// <returns>Token reader</returns>
		/// <exception cref="UhtTokenException">Thrown if text is not found</exception>
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

		/// <summary>
		/// Require the given text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Required text</param>
		/// <param name="TokenDelegate">Delegate to invoke on a match</param>
		/// <returns>Token reader</returns>
		/// <exception cref="UhtTokenException">Thrown if text is not found</exception>
		public static IUhtTokenReader Require(this IUhtTokenReader TokenReader, string Text, UhtTokenDelegate TokenDelegate)
		{
			return TokenReader.Require(Text, null, TokenDelegate);
		}

		/// <summary>
		/// Require the given text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Required text</param>
		/// <param name="ExceptionContext">Extra exception context</param>
		/// <param name="TokenDelegate">Delegate to invoke on a match</param>
		/// <returns>Token reader</returns>
		/// <exception cref="UhtTokenException">Thrown if text is not found</exception>
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

		/// <summary>
		/// Require the given text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Required text</param>
		/// <param name="ExceptionContext">Extra exception context</param>
		/// <returns>Token reader</returns>
		/// <exception cref="UhtTokenException">Thrown if text is not found</exception>
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

		/// <summary>
		/// Require the given text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Required text</param>
		/// <param name="TokenDelegate">Delegate to invoke on a match</param>
		/// <returns>Token reader</returns>
		/// <exception cref="UhtTokenException">Thrown if text is not found</exception>
		public static IUhtTokenReader Require(this IUhtTokenReader TokenReader, char Text, UhtTokenDelegate TokenDelegate)
		{
			return TokenReader.Require(Text, null, TokenDelegate);
		}

		/// <summary>
		/// Require the given text
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Text">Required text</param>
		/// <param name="ExceptionContext">Extra exception context</param>
		/// <param name="TokenDelegate">Delegate to invoke on a match</param>
		/// <returns>Token reader</returns>
		/// <exception cref="UhtTokenException">Thrown if text is not found</exception>
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
