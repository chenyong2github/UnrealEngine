// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;

namespace EpicGames.UHT.Tokenizer
{
	[Flags]
	public enum UhtCppIdentifierOptions
	{
		None,
		AllowTemplates = 1 << 0,
		AllTokens = 1 << 1, // AllowTemplates implies AllTokens
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

		public static IUhtTokenReader RequireIdentifier(this IUhtTokenReader TokenReader, UhtTokenDelegate TokenDelegate)
		{
			return TokenReader.RequireIdentifier(null, TokenDelegate);
		}

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

				if (Options.HasAnyFlags(UhtCppIdentifierOptions.AllowTemplates | UhtCppIdentifierOptions.AllTokens))
				{
					bool bAllowTemplates = Options.HasAnyFlags(UhtCppIdentifierOptions.AllowTemplates);

					while (true)
					{
						if (bAllowTemplates && TokenReader.TryPeekOptional('<'))
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

		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, UhtCppIdentifierOptions Options = UhtCppIdentifierOptions.None)
		{
			TokenReader.GetCppIdentifier(Options);
			return TokenReader;
		}

		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, ref UhtToken InitialIdentifier, UhtCppIdentifierOptions Options = UhtCppIdentifierOptions.None)
		{
			TokenReader.GetCppIdentifier(ref InitialIdentifier, Options);
			return TokenReader;
		}

		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, UhtTokenListDelegate TokenListDelegate)
		{
			TokenReader.RequireCppIdentifier(UhtCppIdentifierOptions.None, TokenListDelegate);
			return TokenReader;
		}

		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, UhtCppIdentifierOptions Options, UhtTokenListDelegate TokenListDelegate)
		{
			UhtTokenList TokenList = TokenReader.GetCppIdentifier(Options);
			TokenListDelegate(TokenList);
			UhtTokenListCache.Return(TokenList);
			return TokenReader;
		}

		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, ref UhtToken InitialIdentifier, UhtTokenListDelegate TokenListDelegate)
		{
			TokenReader.RequireCppIdentifier(ref InitialIdentifier, UhtCppIdentifierOptions.None, TokenListDelegate);
			return TokenReader;
		}

		public static IUhtTokenReader RequireCppIdentifier(this IUhtTokenReader TokenReader, ref UhtToken InitialIdentifier, UhtCppIdentifierOptions Options, UhtTokenListDelegate TokenListDelegate)
		{
			UhtTokenList TokenList = TokenReader.GetCppIdentifier(ref InitialIdentifier, Options);
			TokenListDelegate(TokenList);
			UhtTokenListCache.Return(TokenList);
			return TokenReader;
		}

		public static UhtTokenList GetCppIdentifier(this IUhtTokenReader TokenReader, UhtCppIdentifierOptions Options = UhtCppIdentifierOptions.None)
		{
			UhtToken Token = TokenReader.GetIdentifier();
			return TokenReader.GetCppIdentifier(ref Token, Options);
		}

		public static UhtTokenList GetCppIdentifier(this IUhtTokenReader TokenReader, ref UhtToken InitialIdentifier, UhtCppIdentifierOptions Options = UhtCppIdentifierOptions.None)
		{
			UhtTokenList ListHead = UhtTokenListCache.Borrow(InitialIdentifier);
			UhtTokenList ListTail = ListHead;

			if (Options.HasAnyFlags(UhtCppIdentifierOptions.AllowTemplates | UhtCppIdentifierOptions.AllTokens))
			{
				bool bAllowTemplates = Options.HasAnyFlags(UhtCppIdentifierOptions.AllowTemplates);

				while (true)
				{
					if (bAllowTemplates && TokenReader.TryPeekOptional('<'))
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
