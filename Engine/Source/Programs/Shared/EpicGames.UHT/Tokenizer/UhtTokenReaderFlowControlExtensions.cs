// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;

namespace EpicGames.UHT.Tokenizer
{
	public static class UhtTokenReaderFlowControlExtensions
	{
		public static bool TryOptionalList(this IUhtTokenReader TokenReader, char Initiator, char Terminator, char Separator, bool bAllowTrailingSeparator, Action Action)
		{
			if (TokenReader.TryOptional(Initiator))
			{
				TokenReader.RequireList(Terminator, Separator, bAllowTrailingSeparator, Action);
				return true;
			}
			return false;
		}

		public static IUhtTokenReader RequireList(this IUhtTokenReader TokenReader, char Initiator, char Terminator, char Separator, bool bAllowTrailingSeparator, Action Action)
		{
			return TokenReader.RequireList(Initiator, Terminator, Separator, bAllowTrailingSeparator, null, Action);
		}

		public static IUhtTokenReader RequireList(this IUhtTokenReader TokenReader, char Initiator, char Terminator, char Separator, bool bAllowTrailingSeparator, object? ExceptionContext, Action Action)
		{
			TokenReader.Require(Initiator);
			return TokenReader.RequireList(Terminator, Separator, bAllowTrailingSeparator, ExceptionContext, Action);
		}

		public static IUhtTokenReader RequireList(this IUhtTokenReader TokenReader, char Terminator, char Separator, bool bAllowTrailingSeparator, Action Action)
		{
			return TokenReader.RequireList(Terminator, Separator, bAllowTrailingSeparator, null, Action);
		}

		public static IUhtTokenReader RequireList(this IUhtTokenReader TokenReader, char Terminator, char Separator, bool bAllowTrailingSeparator, object? ExceptionContext, Action Action)
		{
			// Check for an empty list
			if (TokenReader.TryOptional(Terminator))
			{
				return TokenReader;
			}

			// Process the body
			while (true)
			{

				// Read the element via the lambda
				Action();

				// Make sure we haven't reached the EOF
				if (TokenReader.bIsEOF)
				{
					throw new UhtTokenException(TokenReader, TokenReader.GetToken(), $"'{Separator}' or '{Terminator}'", ExceptionContext);
				}

				// If we have a separator, then it might be a trailing separator 
				if (TokenReader.TryOptional(Separator))
				{
					if (bAllowTrailingSeparator && TokenReader.TryOptional(Terminator))
					{
						return TokenReader;
					}
					continue;
				}

				// Otherwise, we must have an terminator
				if (!TokenReader.TryOptional(Terminator))
				{
					throw new UhtTokenException(TokenReader, TokenReader.GetToken(), $"'{Separator}' or '{Terminator}'", ExceptionContext);
				}
				return TokenReader;
			}
		}

		public static bool TryOptionalList(this IUhtTokenReader TokenReader, char Initiator, char Terminator, char Separator, bool bAllowTrailingSeparator, UhtTokensDelegate Delegate)
		{
			if (TokenReader.TryOptional(Initiator))
			{
				TokenReader.RequireList(Terminator, Separator, bAllowTrailingSeparator, Delegate);
				return true;
			}
			return false;
		}

		public static IUhtTokenReader RequireList(this IUhtTokenReader TokenReader, char Initiator, char Terminator, char Separator, bool bAllowTrailingSeparator, UhtTokensDelegate Delegate)
		{
			return TokenReader.RequireList(Initiator, Terminator, Separator, bAllowTrailingSeparator, null, Delegate);
		}

		public static IUhtTokenReader RequireList(this IUhtTokenReader TokenReader, char Initiator, char Terminator, char Separator, bool bAllowTrailingSeparator, object? ExceptionContext, UhtTokensDelegate Delegate)
		{
			TokenReader.Require(Initiator);
			return TokenReader.RequireList(Terminator, Separator, bAllowTrailingSeparator, ExceptionContext, Delegate);
		}

		public static IUhtTokenReader RequireList(this IUhtTokenReader TokenReader, char Terminator, char Separator, bool bAllowTrailingSeparator, UhtTokensDelegate Delegate)
		{
			return TokenReader.RequireList(Terminator, Separator, bAllowTrailingSeparator, null, Delegate);
		}

		public static IUhtTokenReader RequireList(this IUhtTokenReader TokenReader, char Terminator, char Separator, bool bAllowTrailingSeparator, object? ExceptionContext, UhtTokensDelegate Delegate)
		{
			// Check for an empty list
			if (TokenReader.TryOptional(Terminator))
			{
				return TokenReader;
			}

			// Process the body
			while (true)
			{
				List<UhtToken> Tokens = new List<UhtToken>();

				// Read the tokens until we hit the end
				while (true)
				{
					// Make sure we haven't reached the EOF
					if (TokenReader.bIsEOF)
					{
						throw new UhtTokenException(TokenReader, TokenReader.GetToken(), $"'{Separator}' or '{Terminator}'", ExceptionContext);
					}

					// If we have a separator, then it might be a trailing separator 
					if (TokenReader.TryOptional(Separator))
					{
						Delegate(Tokens);
						if (TokenReader.TryOptional(Terminator))
						{
							if (bAllowTrailingSeparator)
							{
								return TokenReader;
							}
							throw new UhtException(TokenReader, $"A separator '{Separator}' followed immediately by the terminator '{Terminator}' is invalid");
						}
						break;
					}

					// If this is the terminator, then we are done
					if (TokenReader.TryOptional(Terminator))
					{
						Delegate(Tokens);
						return TokenReader;
					}

					Tokens.Add(TokenReader.GetToken());
				}
			}
		}

		/// <summary>
		/// Consume a block of tokens bounded by the two given symbols.
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Initiator">The next token must be the given symbol.</param>
		/// <param name="Terminator">The tokens are read until the given symbol is found.  The terminating symbol will be consumed.</param>
		/// <param name="ExceptionContext">Extra context for any error messages</param>
		/// <returns>The input token reader</returns>
		public static IUhtTokenReader RequireList(this IUhtTokenReader TokenReader, char Initiator, char Terminator, object? ExceptionContext = null)
		{
			TokenReader.Require(Initiator);

			// Process the body
			while (true)
			{

				// Make sure we haven't reached the EOF
				if (TokenReader.bIsEOF)
				{
					throw new UhtTokenException(TokenReader, TokenReader.GetToken(), $"'{Terminator}'", ExceptionContext);
				}

				// Look for the terminator
				if (TokenReader.TryOptional(Terminator))
				{
					break;
				}

				// Consume the current token
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}

		public static IUhtTokenReader While(this IUhtTokenReader TokenReader, string Text, Action Action)
		{
			while (TokenReader.TryOptional(Text))
			{
				Action();
			}
			return TokenReader;
		}

		public static IUhtTokenReader While(this IUhtTokenReader TokenReader, char Text, Action Action)
		{
			while (TokenReader.TryOptional(Text))
			{
				Action();
			}
			return TokenReader;
		}

		/// <summary>
		/// Read tokens until the delegate return false.  The terminating token is not consumed.
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Delegate">Invoked with each read token.  Return true to continue tokenizing or false to terminate.</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader While(this IUhtTokenReader TokenReader, UhtTokensUntilDelegate Delegate)
		{
			while (true)
			{
				if (TokenReader.bIsEOF)
				{
					throw new UhtTokenException(TokenReader, TokenReader.GetToken(), null, null);
				}
				ref UhtToken Token = ref TokenReader.PeekToken();
				if (Delegate(ref Token))
				{
					TokenReader.ConsumeToken();
				}
				else
				{
					return TokenReader;
				}
			}
		}

		public static int ConsumeUntil(this IUhtTokenReader TokenReader, string[] Terminators)
		{
			int ConsumedTokens = 0;
			while (!TokenReader.bIsEOF)
			{
				ref UhtToken Token = ref TokenReader.PeekToken();
				foreach (string Candidate in Terminators)
				{
					if ((Token.IsIdentifier() || Token.IsSymbol()) && Token.IsValue(Candidate))
					{
						return ConsumedTokens;
					}
				}
				TokenReader.ConsumeToken();
				++ConsumedTokens;
			}
			return ConsumedTokens;
		}

		public static IUhtTokenReader ConsumeUntil(this IUhtTokenReader TokenReader, char Terminator)
		{
			while (!TokenReader.bIsEOF)
			{
				if (TokenReader.TryOptional(Terminator))
				{
					break;
				}
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}
	}
}
