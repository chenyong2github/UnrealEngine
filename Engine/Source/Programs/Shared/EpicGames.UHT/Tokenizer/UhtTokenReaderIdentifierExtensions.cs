// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;

namespace EpicGames.UHT.Tokenizer
{

	/// <summary>
	/// Options when parsing identifier
	/// </summary>
	[Flags]
	public enum UhtCppIdentifierOptions
	{

		/// <summary>
		/// No options
		/// </summary>
		None,

		/// <summary>
		/// Include template arguments when parsing identifier
		/// </summary>
		AllowTemplates = 1 << 0,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtGetCppIdentifierOptionsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtCppIdentifierOptions InFlags, UhtCppIdentifierOptions TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtCppIdentifierOptions InFlags, UhtCppIdentifierOptions TestFlags)
		{
			return (InFlags & TestFlags) == TestFlags;
		}

		/// <summary>
		/// Test to see if a specific set of flags have a specific value.
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <param name="MatchFlags">Expected value of the tested flags</param>
		/// <returns>True if the given flags have a specific value.</returns>
		public static bool HasExactFlags(this UhtCppIdentifierOptions InFlags, UhtCppIdentifierOptions TestFlags, UhtCppIdentifierOptions MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	/// <summary>
	/// Collection of token reader extensions for working with identifiers
	/// </summary>
	public static class UhtTokenReaderIdentifierExtensions
	{

		/// <summary>
		/// Get the next token and verify that it is an identifier
		/// </summary>
		/// <returns>True if it is an identifier, false if not.</returns>
		public static bool TryOptionalIdentifier(this IUhtTokenReader TokenReader)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier())
			{
				TokenReader.ConsumeToken();
				return true;
			}
			return false;
		}

		/// <summary>
		/// Get the next token and verify that it is an identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Identifier">The fetched value of the identifier</param>
		/// <returns>True if it is an identifier, false if not.</returns>
		public static bool TryOptionalIdentifier(this IUhtTokenReader TokenReader, out UhtToken Identifier)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier())
			{
				Identifier = Token;
				TokenReader.ConsumeToken();
				return true;
			}
			Identifier = new UhtToken();
			return false;
		}

		/// <summary>
		/// Parse an optional identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="TokenDelegate">Invoked of an identifier is parsed</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader OptionalIdentifier(this IUhtTokenReader TokenReader, UhtTokenDelegate TokenDelegate)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier())
			{
				UhtToken TokenCopy = Token;
				TokenReader.ConsumeToken();
				TokenDelegate(ref TokenCopy);
			}
			return TokenReader;
		}

		/// <summary>
		/// Parse a required identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="ExceptionContext">Extra exception context</param>
		/// <returns>Token reader</returns>
		/// <exception cref="UhtTokenException">Thrown if an identifier isn't found</exception>
		public static IUhtTokenReader RequireIdentifier(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier())
			{
				TokenReader.ConsumeToken();
				return TokenReader;
			}
			throw new UhtTokenException(TokenReader, Token, "an identifier", ExceptionContext);
		}

		/// <summary>
		/// Parse a required identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="TokenDelegate">Invoked if an identifier is parsed</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader RequireIdentifier(this IUhtTokenReader TokenReader, UhtTokenDelegate TokenDelegate)
		{
			return TokenReader.RequireIdentifier(null, TokenDelegate);
		}

		/// <summary>
		/// Parse a required identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="ExceptionContext">Extra exception context</param>
		/// <param name="TokenDelegate">Invoked if an identifier is parsed</param>
		/// <returns>Token reader</returns>
		/// <exception cref="UhtTokenException">Thrown if an identifier isn't found</exception>
		public static IUhtTokenReader RequireIdentifier(this IUhtTokenReader TokenReader, object? ExceptionContext, UhtTokenDelegate TokenDelegate)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier())
			{
				UhtToken CurrentToken = Token;
				TokenReader.ConsumeToken();
				TokenDelegate(ref CurrentToken);
				return TokenReader;
			}
			throw new UhtTokenException(TokenReader, Token, "an identifier", ExceptionContext);
		}

		/// <summary>
		/// Get a required identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="ExceptionContext">Extra exception context</param>
		/// <returns>Identifier token</returns>
		/// <exception cref="UhtTokenException">Thrown if an identifier isn't found</exception>
		public static UhtToken GetIdentifier(this IUhtTokenReader TokenReader, object? ExceptionContext = null)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier())
			{
				UhtToken CurrentToken = Token;
				TokenReader.ConsumeToken();
				return CurrentToken;
			}
			throw new UhtTokenException(TokenReader, Token, ExceptionContext != null ? ExceptionContext : "an identifier");
		}

		/// <summary>
		/// Parse an optional cpp identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Identifier">Enumeration of the identifier tokens</param>
		/// <param name="Options">Identifier options</param>
		/// <returns>True if an identifier is parsed</returns>
		public static bool TryOptionalCppIdentifier(this IUhtTokenReader TokenReader, [NotNullWhen(true)] out IEnumerable<UhtToken>? Identifier, UhtCppIdentifierOptions Options = UhtCppIdentifierOptions.None)
		{
			Identifier = null;
			List<UhtToken> LocalIdentifier = new List<UhtToken>();

			using (var SavedState = new UhtTokenSaveState(TokenReader))
			{
				UhtToken Token;

				if (TokenReader.TryOptionalIdentifier(out Token))
				{
					LocalIdentifier.Add(Token);
				}
				else
				{
					return false;
				}

				if (Options.HasAnyFlags(UhtCppIdentifierOptions.AllowTemplates))
				{
					while (true)
					{
						if (TokenReader.TryPeekOptional('<'))
						{
							LocalIdentifier.Add(TokenReader.GetToken());
							int NestedScopes = 1;
							while (NestedScopes > 0)
							{
								Token = TokenReader.GetToken();
								if (Token.TokenType.IsEndType())
								{
									return false;
								}
								LocalIdentifier.Add(Token);
								if (Token.IsSymbol('<'))
								{
									++NestedScopes;
								}
								else if (Token.IsSymbol('>'))
								{
									--NestedScopes;
								}
							}
						}

						if (!TokenReader.TryPeekOptional("::"))
						{
							break;
						}
						LocalIdentifier.Add(TokenReader.GetToken());
						if (!TokenReader.TryOptionalIdentifier(out Token))
						{
							LocalIdentifier.Add(Token);
						}
						else
						{
							return false;
						}
					}
				}
				else
				{
					while (TokenReader.TryOptional("::"))
					{
						if (TokenReader.TryOptionalIdentifier(out Token))
						{
							LocalIdentifier.Add(Token);
						}
						else
						{
							return false;
						}
					}
				}
				Identifier = LocalIdentifier;
				SavedState.AbandonState();
				return true;
			}
		}

		/// <summary>
		/// Parse a required cpp identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Options">Parsing options</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, UhtCppIdentifierOptions Options = UhtCppIdentifierOptions.None)
		{
			TokenReader.GetCppIdentifier(Options);
			return TokenReader;
		}

		/// <summary>
		/// Parse a required cpp identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="InitialIdentifier">Initial token of the identifier</param>
		/// <param name="Options">Parsing options</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, ref UhtToken InitialIdentifier, UhtCppIdentifierOptions Options = UhtCppIdentifierOptions.None)
		{
			TokenReader.GetCppIdentifier(ref InitialIdentifier, Options);
			return TokenReader;
		}

		/// <summary>
		/// Parse a required cpp identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="TokenListDelegate">Invoked when identifier is parsed</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, UhtTokenListDelegate TokenListDelegate)
		{
			TokenReader.RequireCppIdentifier(UhtCppIdentifierOptions.None, TokenListDelegate);
			return TokenReader;
		}

		/// <summary>
		/// Parse a required cpp identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Options">Parsing options</param>
		/// <param name="TokenListDelegate">Invoked when identifier is parsed</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, UhtCppIdentifierOptions Options, UhtTokenListDelegate TokenListDelegate)
		{
			UhtTokenList TokenList = TokenReader.GetCppIdentifier(Options);
			TokenListDelegate(TokenList);
			UhtTokenListCache.Return(TokenList);
			return TokenReader;
		}

		/// <summary>
		/// Parse a required cpp identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="InitialIdentifier">Initial token of the identifier</param>
		/// <param name="TokenListDelegate">Invoked when identifier is parsed</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, ref UhtToken InitialIdentifier, UhtTokenListDelegate TokenListDelegate)
		{
			TokenReader.RequireCppIdentifier(ref InitialIdentifier, UhtCppIdentifierOptions.None, TokenListDelegate);
			return TokenReader;
		}

		/// <summary>
		/// Parse a required cpp identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="InitialIdentifier">Initial token of the identifier</param>
		/// <param name="Options">Parsing options</param>
		/// <param name="TokenListDelegate">Invoked when identifier is parsed</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, ref UhtToken InitialIdentifier, UhtCppIdentifierOptions Options, UhtTokenListDelegate TokenListDelegate)
		{
			UhtTokenList TokenList = TokenReader.GetCppIdentifier(ref InitialIdentifier, Options);
			TokenListDelegate(TokenList);
			UhtTokenListCache.Return(TokenList);
			return TokenReader;
		}

		/// <summary>
		/// Get a required cpp identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Options">Parsing options</param>
		/// <returns>Token list</returns>
		public static UhtTokenList GetCppIdentifier(this IUhtTokenReader TokenReader, UhtCppIdentifierOptions Options = UhtCppIdentifierOptions.None)
		{
			UhtToken Token = TokenReader.GetIdentifier();
			return TokenReader.GetCppIdentifier(ref Token, Options);
		}

		/// <summary>
		/// Get a required cpp identifier
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="InitialIdentifier">Initial token of the identifier</param>
		/// <param name="Options">Parsing options</param>
		/// <returns>Token list</returns>
		public static UhtTokenList GetCppIdentifier(this IUhtTokenReader TokenReader, ref UhtToken InitialIdentifier, UhtCppIdentifierOptions Options = UhtCppIdentifierOptions.None)
		{
			UhtTokenList ListHead = UhtTokenListCache.Borrow(InitialIdentifier);
			UhtTokenList ListTail = ListHead;

			if (Options.HasAnyFlags(UhtCppIdentifierOptions.AllowTemplates))
			{
				while (true)
				{
					if (TokenReader.TryPeekOptional('<'))
					{
						ListTail.Next = UhtTokenListCache.Borrow(TokenReader.GetToken());
						ListTail = ListTail.Next;
						int NestedScopes = 1;
						while (NestedScopes > 0)
						{
							UhtToken Token = TokenReader.GetToken();
							if (Token.TokenType.IsEndType())
							{
								throw new UhtTokenException(TokenReader, Token, new string[] { "<", ">" }, "template");
							}
							ListTail.Next = UhtTokenListCache.Borrow(Token);
							ListTail = ListTail.Next;
							if (Token.IsSymbol('<'))
							{
								++NestedScopes;
							}
							else if (Token.IsSymbol('>'))
							{
								--NestedScopes;
							}
						}
					}

					if (!TokenReader.TryPeekOptional("::"))
					{
						break;
					}
					ListTail.Next = UhtTokenListCache.Borrow(TokenReader.GetToken());
					ListTail = ListTail.Next;
					ListTail.Next = UhtTokenListCache.Borrow(TokenReader.GetIdentifier());
					ListTail = ListTail.Next;
				}
			}
			else if (TokenReader.PeekToken().IsSymbol("::"))
			{
				TokenReader.While("::", () =>
				{
					ListTail.Next = UhtTokenListCache.Borrow(TokenReader.GetIdentifier());
					ListTail = ListTail.Next;
				});
			}
			return ListHead;
		}
	}
}
