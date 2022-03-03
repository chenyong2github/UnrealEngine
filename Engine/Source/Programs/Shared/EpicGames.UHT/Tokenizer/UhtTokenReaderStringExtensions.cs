// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Tokenizer
{
	public static class UhtTokenReaderStringExtensions
	{
		/// <summary>
		/// Get the next token as a string.  If the next token is not a string, no token is consumed.
		/// </summary>
		/// <param name="Value">The string value of the token</param>
		/// <returns>True if the next token was an string, false if not.</returns>
		public static bool TryOptionalConstString(this IUhtTokenReader TokenReader, out StringView Value)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsConstString())
			{
				Value = Token.GetTokenString();
				TokenReader.ConsumeToken();
				return true;
			}
			Value = "";
			return false;
		}

		/// <summary>
		/// Get the next token as a string.  If the next token is not a string, no token is consumed.
		/// </summary>
		/// <returns>True if the next token was an string, false if not.</returns>
		public static IUhtTokenReader OptionalConstString(this IUhtTokenReader TokenReader)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsConstString())
			{
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}

		/// <summary>
		/// Verify that the next token is a string.
		/// </summary>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>True if the next token was a string, false if not.</returns>
		public static IUhtTokenReader RequireConstString(this IUhtTokenReader TokenReader, object? ExceptionContext)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsConstString())
			{
				TokenReader.ConsumeToken();
				return TokenReader;
			}
			throw new UhtTokenException(TokenReader, Token, "constant string", ExceptionContext);
		}

		/// <summary>
		/// Get the next token as a string.  If the next token is not a string, an exception is thrown
		/// </summary>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>The value of the string.</returns>
		public static StringView GetConstString(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsConstString())
			{
				StringView Output = Token.GetTokenString();
				TokenReader.ConsumeToken();
				return Output;
			}
			throw new UhtTokenException(TokenReader, Token, "constant string", ExceptionContext);
		}

		/// <summary>
		/// Get the next token as a quoted string.  If the next token is not a string, an exception is thrown.
		/// Character constants are not considered strings by this routine.
		/// </summary>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>The value of the string.</returns>
		public static StringView GetConstQuotedString(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.TokenType == UhtTokenType.StringConst)
			{
				StringView Output = Token.Value;
				TokenReader.ConsumeToken();
				return Output;
			}
			throw new UhtTokenException(TokenReader, Token, "constant quoted string", ExceptionContext);
		}

		/// <summary>
		/// Get a const string that can optionally be wrapped with a TEXT() macro
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>The value of the string</returns>
		public static StringView GetWrappedConstString(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier("TEXT"))
			{
				TokenReader.ConsumeToken();
				TokenReader.Require('(');
				StringView Out = TokenReader.GetConstString(ExceptionContext);
				TokenReader.Require(')');
				return Out;
			}
			else
			{
				return TokenReader.GetConstString(ExceptionContext);
			}
		}
	}
}
