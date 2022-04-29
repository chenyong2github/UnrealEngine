// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;

namespace EpicGames.UHT.Tokenizer
{

	/// <summary>
	/// Options for GetRawString method
	/// </summary>
	[Flags]
	public enum UhtRawStringOptions
	{

		/// <summary>
		/// No options
		/// </summary>
		None,

		/// <summary>
		/// Don't consider the terminator while in a quoted string
		/// </summary>
		RespectQuotes = 1 << 0,

		/// <summary>
		/// Don't consume the terminator.  It will be parsed later.
		/// </summary>
		DontConsumeTerminator = 1 << 1,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtRawStringOptionsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtRawStringOptions InFlags, UhtRawStringOptions TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtRawStringOptions InFlags, UhtRawStringOptions TestFlags)
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
		public static bool HasExactFlags(this UhtRawStringOptions InFlags, UhtRawStringOptions TestFlags, UhtRawStringOptions MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	/// <summary>
	/// Interface invoked when the parser reaches a '#' preprocessor block
	/// </summary>
	public interface IUhtTokenPreprocessor
	{

		/// <summary>
		/// Parse a preprocessor directive
		/// </summary>
		/// <param name="Token">Token starting the directive.  Will be only the '#'</param>
		/// <param name="bIsBeingIncluded">If true, the directive the source is being included.  Otherwise it is being skipped as part of an #if block</param>
		/// <param name="bClearComments">If true, comments should be cleared</param>
		/// <param name="bIllegalContentsCheck">If true, excluded contents should be checked for unparsed UE macros (i.e. UCLASS) </param>
		/// <returns>True if the source should continue to be included</returns>
		public bool ParsePreprocessorDirective(ref UhtToken Token, bool bIsBeingIncluded, out bool bClearComments, out bool bIllegalContentsCheck);

		/// <summary>
		/// Save the current preprocessor state
		/// </summary>
		public void SaveState();

		/// <summary>
		/// Restore the current preprocessor state
		/// </summary>
		public void RestoreState();
	}

	/// <summary>
	/// Common token reader interfaces for all token reader.  When creating extension methods, use the interface.
	/// </summary>
	public interface IUhtTokenReader : IUhtMessageSite
	{
		/// <summary>
		/// True if the reader is at the end of the stream
		/// </summary>
		public bool bIsEOF { get; }

		/// <summary>
		/// Current input position in the stream by characters
		/// </summary>
		public int InputPos { get; }

		/// <summary>
		/// Current input line in the stream
		/// </summary>
		public int InputLine { get; set; }

		/// <summary>
		/// Preprocessor attached to the token reader
		/// </summary>
		public IUhtTokenPreprocessor? TokenPreprocessor { get; set; }

		/// <summary>
		/// If the reader doesn't have a current token, then read the next token and return a reference to it.
		/// Otherwise return a reference to the current token.
		/// </summary>
		/// <returns>The current token.  Will be invalidated by other calls to ITokenReader</returns>
		public ref UhtToken PeekToken();

		/// <summary>
		/// Mark the current token as being consumed.  Any call to PeekToken or GetToken will read another token.
		/// </summary>
		public void ConsumeToken();

		/// <summary>
		/// Get the next token in the data.  If there is a current token, then that token is returned and marked as consumed.
		/// </summary>
		/// <returns></returns>
		public UhtToken GetToken();

		/// <summary>
		/// Tests to see if the given token is the first token of a line
		/// </summary>
		/// <param name="Token">The token to test</param>
		/// <returns>True if the token is the first token on the line</returns>
		public bool IsFirstTokenInLine(ref UhtToken Token);

		/// <summary>
		/// Skip any whitespace and comments at the current buffer position
		/// </summary>
		public void SkipWhitespaceAndComments();

		/// <summary>
		/// Read the entire next line in the buffer
		/// </summary>
		/// <returns></returns>
		public UhtToken GetLine();

		/// <summary>
		/// Get a view of the buffer being read
		/// </summary>
		/// <param name="StartPos">Starting character offset in the buffer.</param>
		/// <param name="Count">Length of the span</param>
		/// <returns>The string view into the buffer</returns>
		public StringView GetStringView(int StartPos, int Count);

		/// <summary>
		/// Return a string terminated by the given character.
		/// </summary>
		/// <param name="Terminator">The character to stop at.</param>
		/// <param name="Options">Options</param>
		/// <returns>The parsed string</returns>
		public StringView GetRawString(char Terminator, UhtRawStringOptions Options);

		/// <summary>
		/// The current collection of parsed comments.  This does not include any comments parsed as part of a 
		/// call to PeekToken unless ConsumeToken has been invoked after a call to PeekToken.
		/// </summary>
		public ReadOnlySpan<StringView> Comments { get; }

		/// <summary>
		/// Clear the current collection of comments.  Any comments parsed by PeekToken prior to calling ConsomeToken will
		/// not be cleared.
		/// </summary>
		public void ClearComments();

		/// <summary>
		/// Disable the processing of comments.  This is often done when skipping a bulk of the buffer.
		/// </summary>
		/// <returns></returns>
		public void DisableComments();

		/// <summary>
		/// Enable comment collection.
		/// </summary>
		public void EnableComments();

		/// <summary>
		/// If there are any pending comments (due to a PeekToken), commit then so they will be return as current comments
		/// </summary>
		//COMPATIBILITY-TODO - Remove once the struct adding of tooltips is fixed
		public void CommitPendingComments();

		/// <summary>
		/// Save the current parsing state.  There is a limited number of states that can be saved.
		/// Invoke either RestoreState or AbandonState after calling SaveState.
		/// </summary>
		public void SaveState();

		/// <summary>
		/// Restore a previously saved state.
		/// </summary>
		public void RestoreState();

		/// <summary>
		/// Abandon a previously saved state
		/// </summary>
		public void AbandonState();

		/// <summary>
		/// Enable the recording of tokens
		/// </summary>
		public void EnableRecording();

		/// <summary>
		/// Disable the recording of tokens.  Any currently recorded tokens will be removed
		/// </summary>
		public void DisableRecording();

		/// <summary>
		/// Record the given token to the list of recorded tokens
		/// </summary>
		/// <param name="Token">Token to record</param>
		public void RecordToken(ref UhtToken Token);

		/// <summary>
		/// Get the current collection of recorded tokens
		/// </summary>
		public List<UhtToken> RecordedTokens { get; }
	}

	/// <summary>
	/// Represents a list of tokens. Follow the Next chain for each element in the list.
	/// </summary>
	public class UhtTokenList
	{

		/// <summary>
		/// The token
		/// </summary>
		public UhtToken Token { get; set; }

		/// <summary>
		/// The next token in the list
		/// </summary>
		public UhtTokenList? Next { get; set; }

		/// <summary>
		/// Join the tokens in the list
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Seperator">Separator between the tokens</param>
		/// <returns></returns>
		public void Join(StringBuilder Builder, string Seperator = "")
		{
			Builder.Append(this.Token.Value.ToString());
			UhtTokenList List = this;
			while (List.Next != null)
			{
				List = List.Next;
				Builder.Append(Seperator);
				Builder.Append(List.Token.Value.ToString());
			}
		}

		/// <summary>
		/// Join the tokens in the list
		/// </summary>
		/// <param name="Seperator">Separator between the tokens</param>
		/// <returns></returns>
		public string Join(string Seperator = "")
		{
			if (this.Next == null)
			{
				return this.Token.Value.ToString();
			}
			StringBuilder Builder = new StringBuilder();
			Join(Builder, Seperator);
			return Builder.ToString();
		}

		/// <summary>
		/// Return the token list as an array
		/// </summary>
		/// <returns></returns>
		public UhtToken[] ToArray()
		{
			int Count = 1;
			for (UhtTokenList Temp = this; Temp.Next != null; Temp = Temp.Next)
			{
				++Count;
			}
			UhtToken[] Out = new UhtToken[Count];
			Out[0] = this.Token;
			Count = 1;
			for (UhtTokenList Temp = this; Temp.Next != null; Temp = Temp.Next)
			{
				Out[Count] = Temp.Next.Token;
				++Count;
			}
			return Out;
		}
	}

	/// <summary>
	/// Token list cache.  Token lists must be returned to the cache.
	/// </summary>
	public static class UhtTokenListCache
	{
		private static readonly ThreadLocal<UhtTokenList?> Tls = new ThreadLocal<UhtTokenList?>(() => null);

		/// <summary>
		/// Borrow a token list
		/// </summary>
		/// <param name="Token">Starting token</param>
		/// <returns>Token list</returns>
		public static UhtTokenList Borrow(UhtToken Token)
		{
			UhtTokenList? Identifier = Tls.Value;
			if (Identifier != null)
			{
				Tls.Value = Identifier.Next;
			}
			else
			{
				Identifier = new UhtTokenList();
			}
			Identifier.Token = Token;
			Identifier.Next = null;
			return Identifier;
		}

		/// <summary>
		/// Return a token list to the cache
		/// </summary>
		/// <param name="Identifier"></param>
		public static void Return(UhtTokenList Identifier)
		{
			UhtTokenList? Tail = Tls.Value;
			if (Tail != null)
			{
				Tail.Next = Identifier;
			}

			for (; Identifier.Next != null; Identifier = Identifier.Next)
			{
			}

			Tls.Value = Identifier;
		}
	}

	/// <summary>
	/// Delegate for when a token is parsed
	/// </summary>
	/// <param name="Token">The token in question</param>
	public delegate void UhtTokenDelegate(ref UhtToken Token);

	/// <summary>
	/// Delegate for when a token is parsed in an until block
	/// </summary>
	/// <param name="Token">The token in question</param>
	/// <returns>True if parsing should continue</returns>
	public delegate bool UhtTokensUntilDelegate(ref UhtToken Token);

	/// <summary>
	/// Delegate for an enumeration of tokens
	/// </summary>
	/// <param name="Tokens">Parsed tokens</param>
	public delegate void UhtTokensDelegate(IEnumerable<UhtToken> Tokens);

	/// <summary>
	/// Delegate for cached token list
	/// </summary>
	/// <param name="TokenList">Token list that can be cached</param>
	public delegate void UhtTokenListDelegate(UhtTokenList TokenList);

	/// <summary>
	/// Delegate for a constant float
	/// </summary>
	/// <param name="Value">Value in question</param>
	public delegate void UhtTokenConstFloatDelegate(float Value);

	/// <summary>
	/// Delegate for a constant double
	/// </summary>
	/// <param name="Value">Value in question</param>
	public delegate void UhtTokenConstDoubleDelegate(double Value);

	/// <summary>
	/// Helper struct to disable comment parsing.  Should be used in a using block
	/// </summary>
	public struct UhtTokenDisableComments : IDisposable
	{
		private readonly IUhtTokenReader TokenReader;

		/// <summary>
		/// Construct instance
		/// </summary>
		/// <param name="TokenReader">Token reader to disable</param>
		public UhtTokenDisableComments(IUhtTokenReader TokenReader)
		{
			this.TokenReader = TokenReader;
			this.TokenReader.DisableComments();
		}

		/// <summary>
		/// Enable comments
		/// </summary>
		public void Dispose()
		{
			this.TokenReader.EnableComments();
		}
	}

	/// <summary>
	/// Helper struct to save token reader state
	/// </summary>
	public struct UhtTokenSaveState : IDisposable
	{
		private readonly IUhtTokenReader TokenReader;
		private bool bHandled;

		/// <summary>
		/// Construct instance
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		public UhtTokenSaveState(IUhtTokenReader TokenReader)
		{
			this.TokenReader = TokenReader;
			this.bHandled = false;
			this.TokenReader.SaveState();
		}

		/// <summary>
		/// Restore the token reader state
		/// </summary>
		public void Dispose()
		{
			if (!this.bHandled)
			{
				RestoreState();
			}
		}

		/// <summary>
		/// Restore the token reader state
		/// </summary>
		/// <exception cref="UhtIceException">Thrown if state has already been restored or aborted</exception>
		public void RestoreState()
		{
			if (this.bHandled)
			{
				throw new UhtIceException("Can not call RestoreState/AbandonState more than once");
			}
			this.TokenReader.RestoreState();
			this.bHandled = true;
		}

		/// <summary>
		/// Abandon the saved state
		/// </summary>
		/// <exception cref="UhtIceException">Thrown if state has already been restored or aborted</exception>
		public void AbandonState()
		{
			if (this.bHandled)
			{
				throw new UhtIceException("Can not call RestoreState/AbandonState more than once");
			}
			this.TokenReader.AbandonState();
			this.bHandled = true;
		}
	}
}
