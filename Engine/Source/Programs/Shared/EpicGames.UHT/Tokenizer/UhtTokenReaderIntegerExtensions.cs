// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Tokenizer
{
	public delegate bool UhtParseMergedSignToken(ref UhtToken Token);

	public static class UhtTokenReaderIntegerExtensions
	{
		/// <summary>
		/// Get the next token as an integer.  If the next token is not an integer, no token is consumed.
		/// </summary>
		/// <param name="Value">The integer value of the token</param>
		/// <returns>True if the next token was an integer, false if not.</returns>
		public static bool TryOptionalConstInt(this IUhtTokenReader TokenReader, out int Value)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.GetConstInt(out Value))
			{
				TokenReader.ConsumeToken();
				return true;
			}
			Value = 0;
			return false;
		}

		/// <summary>
		/// Get the next token as an integer.  If the next token is not an integer, an exception is thrown
		/// </summary>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>The value of the constant</returns>
		public static IUhtTokenReader OptionalConstInt(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			int Value;
			TokenReader.TryOptionalConstInt(out Value);
			return TokenReader;
		}

		/// <summary>
		/// Get the next token as an integer.  If the next token is not an integer, an exception is thrown
		/// </summary>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>The value of the constant</returns>
		public static IUhtTokenReader RequireConstInt(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			int Value;
			if (!TokenReader.TryOptionalConstInt(out Value))
			{
				throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "constant integer", ExceptionContext);
			}
			return TokenReader;
		}

		/// <summary>
		/// Get the next token as an integer.  If the next token is not an integer, an exception is thrown
		/// </summary>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>The value of the constant</returns>
		public static int GetConstInt(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			int Value;
			if (!TokenReader.TryOptionalConstInt(out Value))
			{
				throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "constant integer", ExceptionContext);
			}
			return Value;
		}

		/// <summary>
		/// Get the next token as an integer.  If the next token is not an integer, no token is consumed.
		/// </summary>
		/// <param name="Value">The integer value of the token</param>
		/// <returns>True if the next token was an integer, false if not.</returns>
		public static bool TryOptionalConstLong(this IUhtTokenReader TokenReader, out long Value)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.GetConstLong(out Value))
			{
				TokenReader.ConsumeToken();
				return true;
			}
			Value = 0;
			return false;
		}

		/// <summary>
		/// Get the next token as an integer.  If the next token is not an integer, an exception is thrown
		/// </summary>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>The value of the constant</returns>
		public static IUhtTokenReader OptionalConstLong(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();

			long Value;
			if (Token.GetConstLong(out Value))
			{
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}

		/// <summary>
		/// Get the next token as an integer.  If the next token is not an integer, an exception is thrown
		/// </summary>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>The value of the constant</returns>
		public static IUhtTokenReader RequireConstLong(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			long Value;
			if (!TokenReader.TryOptionalConstLong(out Value))
			{
				throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "constant long integer", ExceptionContext);
			}
			return TokenReader;
		}

		/// <summary>
		/// Get the next token as an integer.  If the next token is not an integer, an exception is thrown
		/// </summary>
		/// <param name="ExceptionContext">If not null, an exception will be thrown with the given text as part of the message.</param>
		/// <returns>The value of the constant</returns>
		public static long GetConstLong(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			long Value;
			if (!TokenReader.TryOptionalConstLong(out Value))
			{
				throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "constant long integer", ExceptionContext);
			}
			return Value;
		}

		/// <summary>
		/// Helper method to combine any leading sign with the next numeric token
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <param name="Delegate">Delegate to invoke with the merged value</param>
		/// <returns>True if the next token was an parsed, false if not.</returns>
		public static bool TryOptionalLeadingSignConstNumeric(this IUhtTokenReader TokenReader, UhtParseMergedSignToken Delegate)
		{
			using (var SavedState = new UhtTokenSaveState(TokenReader))
			{
				// Check for a leading sign token
				char Sign = ' ';
				UhtToken Token = TokenReader.PeekToken();
				if (Token.IsSymbol() && Token.Value.Length == 1 && UhtFCString.IsSign(Token.Value.Span[0]))
				{
					Sign = Token.Value.Span[0];
					TokenReader.ConsumeToken();
					Token = TokenReader.PeekToken();
					if (UhtFCString.IsSign(Token.Value.Span[0]))
					{
						return false;
					}
					Token.Value = new StringView($"{Sign}{Token.Value}");
				}
				if (Delegate(ref Token))
				{
					TokenReader.ConsumeToken();
					SavedState.AbandonState();
					return true;
				}
				return false;
			}
		}

		/// <summary>
		/// Get the next integer.  It also handled [+/-] token followed by an integer.
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <param name="Value">The integer value of the token</param>
		/// <returns>True if the next token was an integer, false if not.</returns>
		public static bool TryOptionalConstIntExpression(this IUhtTokenReader TokenReader, out int Value)
		{
			int LocalValue = 0;
			bool Results = TokenReader.TryOptionalLeadingSignConstNumeric((ref UhtToken Token) =>
			{
				return Token.IsConstInt() && Token.GetConstInt(out LocalValue);
			});
			Value = LocalValue;
			return Results;
		}

		/// <summary>
		/// Get the next integer.  It also handled [+/-] token followed by an integer.
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <returns>The integer value</returns>
		public static int GetConstIntExpression(this IUhtTokenReader TokenReader)
		{
			int LocalValue = 0;
			bool Results = TokenReader.TryOptionalLeadingSignConstNumeric((ref UhtToken Token) =>
			{
				return Token.IsConstInt() && Token.GetConstInt(out LocalValue);
			});
			return LocalValue;
		}

		/// <summary>
		/// Get the next integer.  It also handled [+/-] token followed by an integer.
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <param name="Value">The integer value of the token</param>
		/// <returns>True if the next token was an integer, false if not.</returns>
		public static bool TryOptionalConstLongExpression(this IUhtTokenReader TokenReader, out long Value)
		{
			long LocalValue = 0;
			bool Results = TokenReader.TryOptionalLeadingSignConstNumeric((ref UhtToken Token) =>
			{
				return Token.IsConstInt() && Token.GetConstLong(out LocalValue);
			});
			Value = LocalValue;
			return Results;
		}

		/// <summary>
		/// Get the next long.  It also handled [+/-] token followed by an long.
		/// </summary>
		/// <param name="TokenReader">Source tokens</param>
		/// <returns>The long value</returns>
		public static int GetConstLongExpression(this IUhtTokenReader TokenReader)
		{
			int LocalValue = 0;
			bool Results = TokenReader.TryOptionalLeadingSignConstNumeric((ref UhtToken Token) =>
			{
				return Token.IsConstInt() && Token.GetConstInt(out LocalValue);
			});
			return LocalValue;
		}
	}
}
