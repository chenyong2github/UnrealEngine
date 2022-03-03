// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;

namespace EpicGames.UHT.Tokenizer
{
	public static class UhtTokenReaderUtilityExtensions
	{
		private static HashSet<StringView> SkipDeclarationWarningStrings = new HashSet<StringView>
			{
				"GENERATED_BODY",
				"GENERATED_IINTERFACE_BODY",
				"GENERATED_UCLASS_BODY",
				"GENERATED_UINTERFACE_BODY",
				"GENERATED_USTRUCT_BODY",
				// Leaving these disabled ATM since they can exist in the code without causing compile issues
				//"RIGVM_METHOD",
				//"UCLASS",
				//"UDELEGATE",
				//"UENUM",
				//"UFUNCTION",
				//"UINTERFACE",
				//"UPROPERTY",
				//"USTRUCT",
			};

		/// <summary>
		/// When processing type, make sure that the next token is the expected token
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Token">Token that started the process</param>
		/// <returns>true if there could be more header to process, false if the end was reached.</returns>
		public static bool SkipExpectedType(this IUhtTokenReader TokenReader, StringView ExpectedIdentifier, bool bIsMember)
		{
			if (TokenReader.TryOptional(ExpectedIdentifier))
			{
				return true;
			}
			if (bIsMember && TokenReader.TryOptional("const"))
			{
				TokenReader.LogError("Const properties are not supported.");
			}
			else
			{
				TokenReader.LogError($"Inappropriate keyword '{TokenReader.PeekToken().Value}' on variable of type '{ExpectedIdentifier}'");
			}
			return false;
		}

		public static bool TryOptionalAPIMacro(this IUhtTokenReader TokenReader, out UhtToken APIMacroToken)
		{
			ref UhtToken Token = ref TokenReader.PeekToken();
			if (Token.IsIdentifier() && Token.Value.Span.EndsWith("_API"))
			{
				APIMacroToken = Token;
				TokenReader.ConsumeToken();
				return true;
			}
			APIMacroToken = new UhtToken();
			return false;
		}

		public static IUhtTokenReader OptionalInheritance(this IUhtTokenReader TokenReader, UhtTokenDelegate SuperClassDelegate)
		{
			TokenReader.Optional(':', () =>
			{
				TokenReader
					.Require("public", "public access modifier")
					.RequireIdentifier(SuperClassDelegate);
			});
			return TokenReader;
		}

		public static IUhtTokenReader OptionalInheritance(this IUhtTokenReader TokenReader, UhtTokenDelegate SuperClassDelegate, UhtTokenListDelegate BaseClassDelegate)
		{
			TokenReader.Optional(':', () =>
			{
				TokenReader
					.Require("public", "public access modifier")
					.RequireIdentifier(SuperClassDelegate)
					.While(',', () =>
					{
						TokenReader
							.Require("public", "public interface access specifier")
							.RequireCppIdentifier(UhtCppIdentifierOptions.AllowTemplates, BaseClassDelegate);
					});
			});
			return TokenReader;
		}

		/// <summary>
		/// Given a declaration/statement that starts with the given token, skip the declaration in the header.
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Token">Token that started the process</param>
		/// <returns>true if there could be more header to process, false if the end was reached.</returns>
		public static bool SkipDeclaration(this IUhtTokenReader TokenReader, ref UhtToken Token)
		{
			UhtToken StarToken = Token;

			// Consume all tokens until the end of declaration/definition has been found.
			int NestedScopes = 0;
			bool bEndOfDeclarationFound = false;

			// Store the current value of PrevComment so it can be restored after we parsed everything.
			using (UhtTokenDisableComments DisableComments = new UhtTokenDisableComments(TokenReader))
			{

				// Check if this is a class/struct declaration in which case it can be followed by member variable declaration.	
				bool bPossiblyClassDeclaration = Token.IsIdentifier() && (Token.IsValue("class") || Token.IsValue("struct"));

				// (known) macros can end without ; or } so use () to find the end of the declaration.
				// However, we don't want to use it with DECLARE_FUNCTION, because we need it to be treated like a function.
				bool bMacroDeclaration = IsProbablyAMacro(Token.Value) && !Token.IsIdentifier("DECLARE_FUNCTION");

				bool bDefinitionFound = false;
				char OpeningBracket = bMacroDeclaration ? '(' : '{';
				char ClosingBracket = bMacroDeclaration ? ')' : '}';

				bool bRetestCurrentToken = false;
				while (true)
				{
					if (!bRetestCurrentToken)
					{
						Token = TokenReader.GetToken();
						if (Token.TokenType.IsEndType())
						{
							break;
						}
					}
					else
					{
						bRetestCurrentToken = false;
					}
					ReadOnlySpan<char> Span = Token.Value.Span;

					// If this is a macro, consume it
					// If we find parentheses at top-level and we think it's a class declaration then it's more likely
					// to be something like: class UThing* GetThing();
					if (bPossiblyClassDeclaration && NestedScopes == 0 && Token.IsSymbol() && Span.Length == 1 && Span[0] == '(')
					{
						bPossiblyClassDeclaration = false;
					}

					if (Token.IsSymbol() && Span.Length == 1 && Span[0] == ';' && NestedScopes == 0)
					{
						bEndOfDeclarationFound = true;
						break;
					}

					if (Token.IsIdentifier())
					{
						// Use a trivial pre-filter to avoid doing the search on things that aren't UE keywords we care about
						if (Span[0] == 'G' || Span[0] == 'R' || Span[0] == 'U')
						{
							if (SkipDeclarationWarningStrings.Contains(Token.Value))
							{
								TokenReader.LogWarning($"The identifier \'{Token.Value}\' was detected in a block being skipped. Was this intentional?");
							}
						}
					}

					if (!bMacroDeclaration && Token.IsIdentifier() && Span.Equals("PURE_VIRTUAL", StringComparison.Ordinal) && NestedScopes == 0)
					{
						OpeningBracket = '(';
						ClosingBracket = ')';
					}

					if (Token.IsSymbol() && Span.Length == 1 && Span[0] == OpeningBracket)
					{
						// This is a function definition or class declaration.
						bDefinitionFound = true;
						NestedScopes++;
					}
					else if (Token.IsSymbol() && Span.Length == 1 && Span[0] == ClosingBracket)
					{
						NestedScopes--;
						if (NestedScopes == 0)
						{
							// Could be a class declaration in all capitals, and not a macro
							bool bReallyEndDeclaration = true;
							if (bMacroDeclaration)
							{
								bReallyEndDeclaration = !TokenReader.TryPeekOptional('{');
							}

							if (bReallyEndDeclaration)
							{
								bEndOfDeclarationFound = true;
								break;
							}
						}

						if (NestedScopes < 0)
						{
							throw new UhtException(TokenReader, Token.InputLine, $"Unexpected '{ClosingBracket}'. Did you miss a semi-colon?");
						}
					}
					else if (bMacroDeclaration && NestedScopes == 0)
					{
						bMacroDeclaration = false;
						OpeningBracket = '{';
						ClosingBracket = '}';
						bRetestCurrentToken = true;
					}
				}
				if (bEndOfDeclarationFound)
				{
					// Member variable declaration after class declaration (see bPossiblyClassDeclaration).
					if (bPossiblyClassDeclaration && bDefinitionFound)
					{
						// Should syntax errors be also handled when someone declares a variable after function definition?
						// Consume the variable name.
						if (TokenReader.bIsEOF)
						{
							return true;
						}
						if (TokenReader.TryOptionalIdentifier())
						{
							TokenReader.Require(';');
						}
					}

					// C++ allows any number of ';' after member declaration/definition.
					while (TokenReader.TryOptional(';'))
					{ 
					}
				}
			}

			// Successfully consumed C++ declaration unless mismatched pair of brackets has been found.
			return NestedScopes == 0 && bEndOfDeclarationFound;
		}

		private static bool IsProbablyAMacro(StringView Identifier)
		{
			ReadOnlySpan<char> Span = Identifier.Span;
			if (Span.Length == 0)
			{
				return false;
			}

			// Macros must start with a capitalized alphanumeric character or underscore
			char FirstChar = Span[0];
			if (FirstChar != '_' && (FirstChar < 'A' || FirstChar > 'Z'))
			{
				return false;
			}

			// Test for known delegate and event macros.
			if (Span.StartsWith("DECLARE_MULTICAST_DELEGATE") ||
				Span.StartsWith("DECLARE_DELEGATE") ||
				Span.StartsWith("DECLARE_EVENT"))
			{
				return true;
			}

			// Failing that, we'll guess about it being a macro based on it being a fully-capitalized identifier.
			foreach (char Ch in Span.Slice(1))
			{
				if (Ch != '_' && (Ch < 'A' || Ch > 'Z') && (Ch < '0' || Ch > '9'))
				{
					return false;
				}
			}

			return true;
		}
	}
}
