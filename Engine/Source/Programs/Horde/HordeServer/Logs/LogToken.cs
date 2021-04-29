// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Logs
{
	/// <summary>
	/// Functionality for decomposing log text into tokens
	/// </summary>
	public static class LogToken
	{
		/// <summary>
		/// Maximum number of bytes in each token
		/// </summary>
		public const int MaxTokenBytes = 4;

		/// <summary>
		/// Number of bits in each token
		/// </summary>
		public const int MaxTokenBits = MaxTokenBytes * 8;

		/// <summary>
		/// Lookup from input byte to token type
		/// </summary>
		static readonly byte[] TokenTypes = GetTokenTypes();

		/// <summary>
		/// Lookup from input byte to token char
		/// </summary>
		static readonly byte[] TokenChars = GetTokenChars();

		/// <summary>
		/// Get a set of tokens in a trie
		/// </summary>
		/// <param name="Text">Text to scan</param>
		/// <returns></returns>
		public static ReadOnlyTrie GetTrie(ReadOnlySpan<byte> Text)
		{
			ReadOnlyTrieBuilder Builder = new ReadOnlyTrieBuilder();
			GetTokens(Text, Builder.Add);
			return Builder.Build();
		}

		/// <summary>
		/// Gets a single token
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <returns>The token value</returns>
		public static ulong GetToken(ReadOnlySpan<byte> Text)
		{
			ulong Token = 0;
			GetTokens(Text, x => Token = x);
			return Token;
		}

		/// <summary>
		/// Get a set of tokens in a trie
		/// </summary>
		/// <param name="Text">Text to scan</param>
		/// <returns></returns>
		public static HashSet<ulong> GetTokenSet(ReadOnlySpan<byte> Text)
		{
			HashSet<ulong> Tokens = new HashSet<ulong>();
			GetTokens(Text, x => Tokens.Add(x));
			return Tokens;
		}

		/// <summary>
		/// Decompose a span of text into tokens
		/// </summary>
		/// <param name="Text">Text to scan</param>
		/// <param name="AddToken">Receives a set of tokens</param>
		public static void GetTokens(ReadOnlySpan<byte> Text, Action<ulong> AddToken)
		{
			if (Text.Length > 0)
			{
				int Type = TokenTypes[Text[0]];
				int NumTokenBits = 8;
				ulong Token = TokenChars[Text[0]];

				for (int TextIdx = 1; TextIdx < Text.Length; TextIdx++)
				{
					byte NextChar = TokenChars[Text[TextIdx]];
					int NextType = TokenTypes[NextChar];
					if (Type != NextType || NumTokenBits + 8 > MaxTokenBits)
					{
						AddToken(Token << (MaxTokenBits - NumTokenBits));
						Token = 0;
						NumTokenBits = 0;
						Type = NextType;
					}
					Token = (Token << 8) | NextChar;
					NumTokenBits += 8;
				}

				AddToken(Token << (MaxTokenBits - NumTokenBits));
			}
		}

		/// <summary>
		/// Gets the length of the first token in the given span
		/// </summary>
		/// <param name="Text">The text to search</param>
		/// <param name="Pos">Start position for the search</param>
		/// <returns>Length of the first token</returns>
		public static ReadOnlySpan<byte> GetTokenText(ReadOnlySpan<byte> Text, int Pos)
		{
			int Type = TokenTypes[Text[Pos]];
			for (int End = Pos + 1; ; End++)
			{
				if (End == Text.Length || TokenTypes[Text[End]] != Type)
				{
					return Text.Slice(Pos, End - Pos);
				}
			}
		}

		/// <summary>
		/// Gets the length of the first token in the given span
		/// </summary>
		/// <param name="Text">The text to search</param>
		/// <param name="Offset">Offset of the window to read from the token</param>
		/// <returns>Length of the first token</returns>
		public static ulong GetWindowedTokenValue(ReadOnlySpan<byte> Text, int Offset)
		{
			ulong Token = 0;
			for (int Idx = 0; Idx < MaxTokenBytes; Idx++)
			{
				Token <<= 8;
				if (Offset >= 0 && Offset < Text.Length)
				{
					Token |= TokenChars[Text[Offset]];
				}
				Offset++;
			}
			return Token;
		}

		/// <summary>
		/// Gets the length of the first token in the given span
		/// </summary>
		/// <param name="Text">The text to search</param>
		/// <param name="Offset">Offset of the window to read from the token</param>
		/// <param name="bAllowPartialMatch">Whether to allow only matching the start of the string</param>
		/// <returns>Length of the first token</returns>
		public static ulong GetWindowedTokenMask(ReadOnlySpan<byte> Text, int Offset, bool bAllowPartialMatch)
		{
			ulong Token = 0;
			for (int Idx = 0; Idx < MaxTokenBytes; Idx++)
			{
				Token <<= 8;
				if (Offset >= 0 && (Offset < Text.Length || !bAllowPartialMatch))
				{
					Token |= 0xff;
				}
				Offset++;
			}
			return Token;
		}

		/// <summary>
		/// Build the lookup table for token types
		/// </summary>
		/// <returns>Array whose elements map from an input byte to token type</returns>
		static byte[] GetTokenTypes()
		{
			byte[] CharTypes = new byte[256];
			for (int Idx = 'a'; Idx <= 'z'; Idx++)
			{
				CharTypes[Idx] = 1;
			}
			for (int Idx = 'A'; Idx <= 'Z'; Idx++)
			{
				CharTypes[Idx] = 1;
			}
			for (int Idx = '0'; Idx <= '9'; Idx++)
			{
				CharTypes[Idx] = 2;
			}
			CharTypes[' '] = 3;
			CharTypes['\t'] = 3;
			CharTypes['\n'] = 4;
			return CharTypes;
		}

		/// <summary>
		/// Build the lookup table for token types
		/// </summary>
		/// <returns>Array whose elements map from an input byte to token type</returns>
		static byte[] GetTokenChars()
		{
			byte[] Chars = new byte[256];
			for(int Idx = 0; Idx < 256; Idx++)
			{
				Chars[Idx] = (byte)Idx;
			}
			for (int Idx = 'A'; Idx <= 'Z'; Idx++)
			{
				Chars[Idx] = (byte)('a' + Idx - 'A');
			}
			return Chars;
		}
	}
}
