// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace EpicGames.UHT.Tokenizer
{
	public class UhtTokenBufferReader : IUhtTokenReader, IUhtMessageLineNumber
	{
		private static StringView[] EmptyComments = new StringView[] { };

		private readonly IUhtMessageSite MessageSiteInternal;
		private List<StringView>? CommentsInternal = null;
		private List<UhtToken> RecordedTokensInternal = new List<UhtToken>();
		private IUhtTokenPreprocessor? TokenPreprocessorInternal = null;
		private UhtToken CurrentToken = new UhtToken(); // PeekToken must have been invoked first
		private ReadOnlyMemory<char> Data;
		private int PrevPos = 0;
		private int PrevLine = 1;
		private bool bHasToken = false;
		private int PreCurrentTokenInputPos = 0;
		private int PreCurrentTokenInputLine = 1;
		private int InputPosInternal = 0;
		private int InputLineInternal = 1;
		private int CommentsDisableCount = 0;
		private int CommittedComments = 0;
		private List<StringView>? SavedComments = null;
		private int SavedInputPos = 0;
		private int SavedInputLine = 0;
		private bool bHasSavedState = false;
		private bool bRecordTokens = false;
		private int PreprocessorPendingCommentsCount = 0;

		public UhtTokenBufferReader(IUhtMessageSite MessageSite, ReadOnlyMemory<char> Input)
		{
			this.MessageSiteInternal = MessageSite;
			this.Data = Input;
		}

		#region IUHTMessageSite Implementation
		IUhtMessageSession IUhtMessageSite.MessageSession => this.MessageSiteInternal.MessageSession;
		IUhtMessageSource? IUhtMessageSite.MessageSource => this.MessageSiteInternal.MessageSource;
		IUhtMessageLineNumber? IUhtMessageSite.MessageLineNumber => this;
		#endregion

		#region IUHTMessageLinenumber Implementation
		int IUhtMessageLineNumber.MessageLineNumber => this.InputLine;
		#endregion

		#region ITokenReader Implementation
		public bool bIsEOF
		{
			get
			{
				if (this.bHasToken)
				{
					return this.CurrentToken.TokenType.IsEndType();
				}
				else
				{
					return this.InputPos == this.Data.Length;
				}
			}
		}

		public int InputPos
		{
			get => this.bHasToken ? this.PreCurrentTokenInputPos : this.InputPosInternal;
		}

		public int InputLine
		{
			get => this.bHasToken ? this.PreCurrentTokenInputLine : this.InputLineInternal;
			set { ClearToken(); this.InputLineInternal = value; }
		}

		public ReadOnlySpan<StringView> Comments 
		{
			get
			{
				if (this.CommentsInternal != null && this.CommittedComments != 0)
				{
					return new ReadOnlySpan<StringView>(this.CommentsInternal.ToArray(), 0, this.CommittedComments);
				}
				else
				{
					return new ReadOnlySpan<StringView>(UhtTokenBufferReader.EmptyComments);
				}
			}
		}

		public IUhtTokenPreprocessor? TokenPreprocessor { get => this.TokenPreprocessorInternal; set => this.TokenPreprocessorInternal = value; }

		public ref UhtToken PeekToken()
		{
			if (!this.bHasToken)
			{
				this.CurrentToken = GetTokenInternal(true);
				this.bHasToken = true;
			}
			return ref this.CurrentToken;
		}

		/// <summary>
		/// Skip all leading whitespace and collect any comments
		/// </summary>
		public void SkipWhitespaceAndComments()
		{
			bool bGotInlineComment = false;
			SkipWhitespaceAndCommentsInternal(ref bGotInlineComment, true);
		}

		/// <inheritdoc/>
		public void ConsumeToken()
		{
			if (this.bRecordTokens && !this.CurrentToken.IsEndType())
			{
				this.RecordedTokensInternal.Add(this.CurrentToken);
			}
			this.bHasToken = false;

			// When comments are disabled, we are still collecting comments, but aren't committing them
			if (this.CommentsDisableCount == 0)
			{
				if (this.CommentsInternal != null)
				{
					this.CommittedComments = this.CommentsInternal.Count;
				}
			}
			else
			{
				ClearPendingComments();
			}
		}

		/// <inheritdoc/>
		public UhtToken GetToken()
		{
			UhtToken Token = PeekToken();
			ConsumeToken();
			return Token;
		}

		/// <inheritdoc/>
		public StringView GetRawString(char Terminator, UhtRawStringOptions Options)
		{
			ReadOnlySpan<char> Span = this.Data.Span;

			ClearToken();
			SkipWhitespaceAndComments();

			int StartPos = this.InputPos;
			bool bInQuotes = false;
			while (true)
			{
				char C = InternalGetChar(Span);

				// Check for end of file
				if (C == 0)
				{
					break;
				}

				// Check for end of line
				if (C == '\r' || C == '\n')
				{
					--this.InputPosInternal;
					break;
				}

				// Check for terminator as long as we aren't in quotes
				if (C == Terminator && !bInQuotes)
				{
					if (Options.HasAnyFlags(UhtRawStringOptions.DontConsumeTerminator))
					{
						--this.InputPosInternal;
					}
					break;
				}

				// Check for comment
				if (!bInQuotes && C == '/')
				{
					char P = InternalPeekChar(Span);
					if (P == '*' || P == '/')
					{
						--this.InputPosInternal;
						break;
					}
				}

				// Check for quotes
				if (C == '"' && Options.HasAnyFlags(UhtRawStringOptions.RespectQuotes))
				{
					bInQuotes = !bInQuotes;
				}
			}

			// If EOF, then error
			if (bInQuotes)
			{
				throw new UhtException(this, "Unterminated quoted string");
			}

			// Remove trailing whitespace
			int EndPos = this.InputPos;
			for (; EndPos > StartPos; --EndPos)
			{
				char C = Span[EndPos - 1];
				if (C != ' ' && C != '\t')
				{
					break;
				}
			}

			// Check for too long
			if (EndPos - StartPos >= UhtToken.MaxStringLength)
			{
				throw new UhtException(this, $"String exceeds maximum of {UhtToken.MaxStringLength} characters");
			}
			return new StringView(this.Data, StartPos, EndPos - StartPos);
		}

		/// <inheritdoc/>
		public UhtToken GetLine()
		{
			ReadOnlySpan<char> Span = this.Data.Span;

			ClearToken();
			this.PrevPos = this.InputPosInternal;
			this.PrevLine = this.InputLineInternal;

			if (this.PrevPos == Span.Length)
			{
				return new UhtToken(UhtTokenType.EndOfFile, this.PrevPos, this.PrevLine, this.PrevPos, this.PrevLine, new StringView(this.Data.Slice(this.PrevPos, 0)));
			}

			int LastPos = this.InputPosInternal;
			while (true)
			{
				char Char = InternalGetChar(Span);
				if (Char == 0)
				{
					break;
				}
				else if (Char == '\r')
				{
				}
				else if (Char == '\n')
				{
					++this.InputLineInternal;
					break;
				}
				else
				{
					LastPos = this.InputPosInternal;
				}
			}

			return new UhtToken(UhtTokenType.Line, this.PrevPos, this.PrevLine, this.PrevPos, this.PrevLine, new StringView(this.Data.Slice(this.PrevPos, LastPos - this.PrevPos)));
		}

		/// <inheritdoc/>
		public StringView GetStringView(int StartPos, int Count)
		{
			return new StringView(this.Data, StartPos, Count);
		}

		/// <inheritdoc/>
		public void ClearComments()
		{
			if (this.PreprocessorPendingCommentsCount != 0)
			{
				Debugger.Break();
			}
			if (this.CommentsInternal != null)
			{

				// Clearing comments does not remove any uncommitted comments
				this.CommentsInternal.RemoveRange(0, this.CommittedComments);
				this.CommittedComments = 0;
			}
		}

		/// <inheritdoc/>
		public void DisableComments()
		{
			++this.CommentsDisableCount;
		}

		/// <inheritdoc/>
		public void EnableComments()
		{
			--this.CommentsDisableCount;
		}

		/// <inheritdoc/>
		public void CommitPendingComments()
		{
			if (this.CommentsInternal != null)
			{
				this.CommittedComments = this.CommentsInternal.Count;
			}
		}

		/// <inheritdoc/>
		public bool IsFirstTokenInLine(ref UhtToken Token)
		{
			return IsFirstTokenInLine(this.Data.Span, Token.InputStartPos);
		}

		/// <inheritdoc/>
		public void SaveState()
		{
			if (this.bHasSavedState)
			{
				throw new UhtIceException("Can not save more than one state");
			}
			this.bHasSavedState = true;
			this.SavedInputLine = this.InputLine;
			this.SavedInputPos = this.InputPos;
			if (this.CommentsInternal != null)
			{
				if (this.SavedComments == null)
				{
					this.SavedComments = new List<StringView>();
				}
				this.SavedComments.Clear();
				this.SavedComments.AddRange(this.CommentsInternal);
			}
			if (this.TokenPreprocessorInternal != null)
			{
				this.TokenPreprocessorInternal.SaveState();
			}
		}

		/// <inheritdoc/>
		public void RestoreState()
		{
			if (!this.bHasSavedState)
			{
				throw new UhtIceException("Can not restore state when none have been saved");
			}
			this.bHasSavedState = false;
			ClearToken();
			this.InputPosInternal = this.SavedInputPos;
			this.InputLineInternal = this.SavedInputLine;
			if (this.SavedComments != null && this.CommentsInternal != null)
			{
				this.CommentsInternal.Clear();
				this.CommentsInternal.AddRange(this.SavedComments);
			}
			if (this.TokenPreprocessorInternal != null)
			{
				this.TokenPreprocessorInternal.RestoreState();
			}
		}

		/// <inheritdoc/>
		public void AbandonState()
		{
			if (!this.bHasSavedState)
			{
				throw new UhtIceException("Can not abandon state when none have been saved");
			}
			this.bHasSavedState = false;
			ClearToken();
		}

		/// <inheritdoc/>
		public void EnableRecording()
		{
			if (this.bRecordTokens)
			{
				throw new UhtIceException("Can not nest token recording");
			}
			this.bRecordTokens = true;
		}

		/// <inheritdoc/>
		public void RecordToken(ref UhtToken Token)
		{
			if (!this.bRecordTokens)
			{
				throw new UhtIceException("Attempt to disable token recording when it isn't enabled");
			}
			this.RecordedTokensInternal.Add(Token);
		}

		/// <inheritdoc/>
		public void DisableRecording()
		{
			if (!this.bRecordTokens)
			{
				throw new UhtIceException("Attempt to disable token recording when it isn't enabled");
			}
			this.RecordedTokensInternal.Clear();
			this.bRecordTokens = false;
		}

		/// <inheritdoc/>
		public List<UhtToken> RecordedTokens
		{
			get
			{
				if (!this.bRecordTokens)
				{
					throw new UhtIceException("Attempt to get recorded tokens when it isn't enabled");
				}
				return this.RecordedTokensInternal;
			}
		}
		#endregion

		#region Internals
		private void ClearAllComments()
		{
			if (this.PreprocessorPendingCommentsCount != 0)
			{
				Debugger.Break();
			}
			if (this.CommentsInternal != null)
			{
				this.CommentsInternal.Clear();
				this.CommittedComments = 0;
			}
		}

		private void ClearPendingComments()
		{
			if (this.CommentsInternal != null)
			{
				int StartingComment = this.CommittedComments + this.PreprocessorPendingCommentsCount;
				this.CommentsInternal.RemoveRange(StartingComment, this.CommentsInternal.Count - StartingComment);
			}
		}

		private bool IsFirstTokenInLine(ReadOnlySpan<char> Span, int StartPos)
		{
			for (int Pos = StartPos; --Pos > 0;)
			{
				switch (Span[Pos])
				{
					case '\r':
					case '\n':
						return true;
					case ' ':
					case '\t':
						break;
					default:
						return false;
				}
			}
			return true;
		}

		/// <summary>
		/// Get the next token
		/// </summary>
		/// <returns>Return the next token from the stream.  If the end of stream is reached, a token type of None will be returned.</returns>
		private UhtToken GetTokenInternal(bool bEnablePreprocessor)
		{
			ReadOnlySpan<char> Span = this.Data.Span;
			int SpanLength = Span.Length;
			bool bGotInlineComment = false;
		Restart:
			this.PreCurrentTokenInputLine = this.InputLineInternal;
			this.PreCurrentTokenInputPos = this.InputPosInternal;
			SkipWhitespaceAndCommentsInternal(ref bGotInlineComment, bEnablePreprocessor);
			this.PrevPos = this.InputPosInternal;
			this.PrevLine = this.InputLineInternal;

			UhtTokenType TokenType = UhtTokenType.EndOfFile;
			int StartPos = this.InputPosInternal;
			int StartLine = this.InputLineInternal;

			char C = InternalGetChar(Span);
			if (C == 0)
			{
			}

			else if (UhtFCString.IsAlpha(C) || C == '_')
			{

				for (; this.InputPosInternal < Span.Length; ++this.InputPosInternal)
				{
					C = Span[this.InputPosInternal];
					if (!(UhtFCString.IsAlpha(C) || UhtFCString.IsDigit(C) || C == '_'))
					{
						break;
					}
				}

				if (this.InputPosInternal - StartPos >= UhtToken.MaxNameLength)
				{
					throw new UhtException(this, $"Identifier length exceeds maximum of {UhtToken.MaxNameLength}");
				}

				TokenType = UhtTokenType.Identifier;
			}

			// Check for any numerics 
			else if (IsNumeric(Span, C))
			{
				// Integer or floating point constant.

				bool bIsFloat = C == '.';
				bool bIsHex = false;

				// Ignore the starting sign for this part of the parsing
				if (UhtFCString.IsSign(C))
				{
					C = InternalGetChar(Span); // We know this won't fail since the code above checked the Peek
				}

				// Check for a hex valid
				if (C == '0')
				{
					bIsHex = UhtFCString.IsHexMarker(InternalPeekChar(Span));
					if (bIsHex)
					{
						InternalGetChar(Span);
					}
				}

				// If we have a hex constant
				if (bIsHex)
				{
					for (; this.InputPosInternal < Span.Length && UhtFCString.IsHexDigit(Span[this.InputPosInternal]); ++this.InputPosInternal)
					{
					}
				}

				// We have decimal/octal value or possibly a floating point value
				else
				{

					// Skip all digits
					for (; this.InputPosInternal < Span.Length && UhtFCString.IsDigit(Span[this.InputPosInternal]); ++this.InputPosInternal)
					{
					}

					// If we have a '.'
					if (this.InputPosInternal < Span.Length && Span[this.InputPosInternal] == '.')
					{
						bIsFloat = true;
						++this.InputPosInternal;

						// Skip all digits
						for (; this.InputPosInternal < Span.Length && UhtFCString.IsDigit(Span[this.InputPosInternal]); ++this.InputPosInternal)
						{
						}
					}

					// If we have a 'e'
					if (this.InputPosInternal < Span.Length && UhtFCString.IsExponentMarker(Span[this.InputPosInternal]))
					{
						bIsFloat = true;
						++this.InputPosInternal;

						// Skip any signs
						if (this.InputPosInternal < Span.Length && UhtFCString.IsSign(Span[this.InputPosInternal]))
						{
							++this.InputPosInternal;
						}

						// Skip all digits
						for (; this.InputPosInternal < Span.Length && UhtFCString.IsDigit(Span[this.InputPosInternal]); ++this.InputPosInternal)
						{
						}
					}

					// If we have a 'f'
					if (this.InputPosInternal < Span.Length && UhtFCString.IsFloatMarker(Span[this.InputPosInternal]))
					{
						bIsFloat = true;
						++this.InputPosInternal;
					}

					// Check for u/l markers
					while (this.InputPosInternal < Span.Length && 
						(UhtFCString.IsUnsignedMarker(Span[this.InputPosInternal]) || UhtFCString.IsLongMarker(Span[this.InputPosInternal])))
					{
						++this.InputPosInternal;
					}
				}

				if (this.InputPosInternal - StartPos >= UhtToken.MaxNameLength)
				{
					throw new UhtException(this, $"Number length exceeds maximum of {UhtToken.MaxNameLength}");
				}

				TokenType = bIsFloat ? UhtTokenType.FloatConst : (bIsHex ? UhtTokenType.HexConst : UhtTokenType.DecimalConst);
			}

			// Escaped character constant
			else if (C == '\'')
			{

				// We try to skip the character constant value. But if it is backslash, we have to skip another character
				char NextChar = InternalGetChar(Span);
				if (NextChar == '\\')
				{
					NextChar = InternalGetChar(Span);
				}

				NextChar = InternalGetChar(Span);
				if (NextChar != '\'')
				{
					throw new UhtException(this, "Unterminated character constant");
				}

				TokenType = UhtTokenType.CharConst;
			}

			// String constant
			else if (C == '"')
			{
				for (; ; )
				{
					char NextChar = InternalGetChar(Span);
					if (NextChar == '\r' || NextChar == '\n')
					{
						// throw
					}

					if (NextChar == '\\')
					{
						NextChar = InternalGetChar(Span);
						if (NextChar == '\r' || NextChar == '\n')
						{
							// throw
						}
						NextChar = InternalGetChar(Span);
					}

					if (NextChar == '"')
					{
						break;
					}
				}

				if (this.InputPosInternal - StartPos >= UhtToken.MaxStringLength)
				{
					throw new UhtException(this, $"String constant exceeds maximum of {UhtToken.MaxStringLength} characters");
				}

				TokenType = UhtTokenType.StringConst;
			}

			// Assume everything else is a symbol.
			// Don't handle >> or >>>
			else
			{
				{
					if (this.InputPosInternal < Span.Length)
					{
						char D = Span[this.InputPosInternal];
						if ((C == '<' && D == '<')
							|| (C == '!' && D == '=')
							|| (C == '<' && D == '=')
							|| (C == '>' && D == '=')
							|| (C == '+' && D == '+')
							|| (C == '-' && D == '-')
							|| (C == '+' && D == '=')
							|| (C == '-' && D == '=')
							|| (C == '*' && D == '=')
							|| (C == '/' && D == '=')
							|| (C == '&' && D == '&')
							|| (C == '|' && D == '|')
							|| (C == '^' && D == '^')
							|| (C == '=' && D == '=')
							|| (C == '*' && D == '*')
							|| (C == '~' && D == '=')
							|| (C == ':' && D == ':'))
						{
							++this.InputPosInternal;
						}
					}

					// Comment processing while processing the preprocessor statements is complicated.  When a preprocessor statement
					// parsers, the tokenizer is doing some form of a PeekToken.  And comments read ahead of the '#' will still be 
					// pending.
					//
					// 1) If the block is being skipped, we want to preserve any comments ahead of the token and eliminate all other comments found in the #if block
					// 2) If the block isn't being skipped or in the case of things such as #pragma or #include, they were considered statements in the UHT
					//	  and would result in the comments being dropped.  In the new UHT, we just eliminate all pending tokens.
					if (C == '#' && bEnablePreprocessor && this.TokenPreprocessorInternal != null && IsFirstTokenInLine(Span, StartPos))
					{
						this.PreprocessorPendingCommentsCount = this.CommentsInternal != null ? this.CommentsInternal.Count - this.CommittedComments : 0;
						UhtToken Temp = new UhtToken(TokenType, StartPos, StartLine, StartPos, this.InputLineInternal, new StringView(this.Data.Slice(StartPos, this.InputPosInternal - StartPos)));
						bool bClearComments = false;
						bool bIllegalContentsCheck = false;
						bool bInclude = this.TokenPreprocessorInternal.ParsePreprocessorDirective(ref Temp, true, out bClearComments, out bIllegalContentsCheck);
						if (!bInclude)
						{
							++this.CommentsDisableCount;
							int CheckStateMachine = 0;
							while (true)
							{
								if (bIsEOF)
								{
									break;
								}
								UhtToken LocalToken = GetTokenInternal(false);
								ReadOnlySpan<char> LocalValueSpan = LocalToken.Value.Span;
								if (LocalToken.IsSymbol('#') && LocalValueSpan.Length == 1 && LocalValueSpan[0] == '#' && IsFirstTokenInLine(Span, LocalToken.InputStartPos))
								{
									if (this.TokenPreprocessorInternal.ParsePreprocessorDirective(ref Temp, false, out bool bScratch, out bool bScratchIllegalContentsCheck))
									{
										break;
									}
									bIllegalContentsCheck = bScratchIllegalContentsCheck;
								}
								else if (bIllegalContentsCheck)
								{
									switch (CheckStateMachine)
									{
										case 0:
										ResetStateMachineCheck:
											if (LocalValueSpan.CompareTo("UPROPERTY", StringComparison.Ordinal) == 0 ||
												LocalValueSpan.CompareTo("UCLASS", StringComparison.Ordinal) == 0 ||
												LocalValueSpan.CompareTo("USTRUCT", StringComparison.Ordinal) == 0 ||
												LocalValueSpan.CompareTo("UENUM", StringComparison.Ordinal) == 0 ||
												LocalValueSpan.CompareTo("UINTERFACE", StringComparison.Ordinal) == 0 ||
												LocalValueSpan.CompareTo("UDELEGATE", StringComparison.Ordinal) == 0 ||
												LocalValueSpan.CompareTo("UFUNCTION", StringComparison.Ordinal) == 0)
											{
												this.LogError($"'{LocalValueSpan.ToString()}' must not be inside preprocessor blocks, except for WITH_EDITORONLY_DATA");
											}
											else if (LocalValueSpan.CompareTo("void", StringComparison.Ordinal) == 0)
											{
												CheckStateMachine = 1;
											}
											break;

										case 1:
											if (LocalValueSpan.CompareTo("Serialize", StringComparison.Ordinal) == 0)
											{
												CheckStateMachine = 2;
											}
											else
											{
												CheckStateMachine = 0;
												goto ResetStateMachineCheck;
											}
											break;

										case 2:
											if (LocalValueSpan.CompareTo("(", StringComparison.Ordinal) == 0)
											{
												CheckStateMachine = 3;
											}
											else
											{
												CheckStateMachine = 0;
												goto ResetStateMachineCheck;
											}
											break;

										case 3:
											if (LocalValueSpan.CompareTo("FArchive", StringComparison.Ordinal) == 0 || 
												LocalValueSpan.CompareTo("FStructuredArchiveRecord", StringComparison.Ordinal) == 0)
											{
												this.LogError($"Engine serialization functions must not be inside preprocessor blocks, except for WITH_EDITORONLY_DATA");
												CheckStateMachine = 0;
											}
											else if (LocalValueSpan.CompareTo("FStructuredArchive", StringComparison.Ordinal) == 0)
											{
												CheckStateMachine = 4;
											}
											else
											{
												CheckStateMachine = 0;
												goto ResetStateMachineCheck;
											}
											break;

										case 4:
											if (LocalValueSpan.CompareTo("::", StringComparison.Ordinal) == 0)
											{
												CheckStateMachine = 5;
											}
											else
											{
												CheckStateMachine = 0;
												goto ResetStateMachineCheck;
											}
											break;

										case 5:
											if (LocalValueSpan.CompareTo("FRecord", StringComparison.Ordinal) == 0)
											{
												this.LogError($"Engine serialization functions must not be inside preprocessor blocks, except for WITH_EDITORONLY_DATA");
												CheckStateMachine = 0;
											}
											else
											{
												CheckStateMachine = 0;
												goto ResetStateMachineCheck;
											}
											break;
									}
								}
							}
							--this.CommentsDisableCount;

							// Clear any extra pending comments at this time
							ClearPendingComments();
						}
						this.PreprocessorPendingCommentsCount = 0;

						// Depending on the type of directive and/or the #if expression, we clear comments.
						if (bClearComments)
						{
							ClearPendingComments();
							bGotInlineComment = false;
						}
						goto Restart;
					}
				}
				TokenType = UhtTokenType.Symbol;
			}

			UhtToken Out = new UhtToken(TokenType, StartPos, StartLine, StartPos, this.InputLineInternal, new StringView(this.Data.Slice(StartPos, this.InputPosInternal - StartPos)));
			return Out;
		}

		/// <summary>
		/// Skip all leading whitespace and collect any comments
		/// </summary>
		private void SkipWhitespaceAndCommentsInternal(ref bool bPersistGotInlineComment, bool bEnablePreprocessor)
		{
			ReadOnlySpan<char> Span = this.Data.Span;

			bool bGotNewlineBetweenComments = false;
			bool bGotInlineComment = bPersistGotInlineComment;
			for (; ; )
			{
				uint Char = InternalGetChar(Span);
				if (Char == 0)
				{
					break;
				}
				else if (Char == '\n')
				{
					bGotNewlineBetweenComments |= bGotInlineComment;
					++this.InputLineInternal;
				}
				else if (Char == '\r' || Char == '\t' || Char == ' ')
				{
				}
				else if (Char == '/')
				{
					uint NextChar = InternalPeekChar(Span);
					if (NextChar == '*')
					{
						if (bEnablePreprocessor)
						{
							ClearAllComments();
						}
						int CommentStart = this.InputPosInternal - 1;
						++this.InputPosInternal;
						for (; ; )
						{
							char CommentChar = InternalGetChar(Span);
							if (CommentChar == 0)
							{
								if (bEnablePreprocessor)
								{
									ClearAllComments();
								}
								throw new UhtException(this, "End of header encountered inside comment");
							}
							else if (CommentChar == '\n')
							{
								++this.InputLineInternal;
							}
							else if (CommentChar == '*' && InternalPeekChar(Span) == '/')
							{
								++this.InputPosInternal;
								break;
							}
						}
						if (bEnablePreprocessor)
						{
							AddComment(new StringView(this.Data.Slice(CommentStart, this.InputPosInternal - CommentStart)));
						}
					}
					else if (NextChar == '/')
					{
						if (bGotNewlineBetweenComments)
						{
							bGotNewlineBetweenComments = false;
							if (bEnablePreprocessor)
							{
								ClearAllComments();
							}
						}
						bGotInlineComment = true;
						int CommentStart = this.InputPosInternal - 1;
						++this.InputPosInternal;

						// Scan to the end of the line
						for (; ; )
						{
							char CommentChar = InternalGetChar(Span);
							if (CommentChar == 0)
							{
								//--Pos;
								break;
							}
							if (CommentChar == '\r')
							{
							}
							else if (CommentChar == '\n')
							{
								++InputLineInternal;
								break;
							}
						}
						if (bEnablePreprocessor)
						{
							AddComment(new StringView(this.Data.Slice(CommentStart, this.InputPosInternal - CommentStart)));
						}
					}
					else
					{
						--this.InputPosInternal;
						break;
					}
				}
				else
				{
					--this.InputPosInternal;
					break;
				}
			}
			bPersistGotInlineComment = bGotInlineComment;
			return;
		}

		private bool IsNumeric(ReadOnlySpan<char> Span, char C)
		{
			// Check for [0..9]
			if (UhtFCString.IsDigit(C))
			{
				return true;
			}

			// Check for [+-]...
			if (UhtFCString.IsSign(C))
			{
				if (this.InputPosInternal == Span.Length)
				{
					return false;
				}

				// Check for [+-][0..9]...
				if (UhtFCString.IsDigit(Span[this.InputPosInternal]))
				{
					return true;
				}

				// Check for [+-][.][0..9]...
				if (Span[this.InputPosInternal] != '.')
				{
					return false;
				}

				if (this.InputPosInternal + 1 == Span.Length)
				{
					return false;
				}

				if (UhtFCString.IsDigit(Span[this.InputPosInternal + 1]))
				{
					return true;
				}
				return false;
			}

			if (C == '.')
			{
				if (this.InputPosInternal == Span.Length)
				{
					return false;
				}

				// Check for [.][0..9]...
				if (UhtFCString.IsDigit(Span[this.InputPosInternal]))
				{
					return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Fetch the next character in the input stream or zero if we have reached the end.
		/// The current offset in the buffer is not advanced.  The method does not support UTF-8
		/// </summary>
		/// <param name="Span">The span containing the data</param>
		/// <returns>Next character in the stream or zero</returns>
		private char InternalPeekChar(ReadOnlySpan<char> Span)
		{
			return this.InputPosInternal < Span.Length ? Span[this.InputPosInternal] : '\0';
		}

		/// <summary>
		/// Fetch the next character in the input stream or zero if we have reached the end.
		/// The current offset in the buffer is advanced.  The method does not support UTF-8.
		/// </summary>
		/// <param name="Span">The span containing the data</param>
		/// <returns>Next character in the stream or zero</returns>
		private char InternalGetChar(ReadOnlySpan<char> Span)
		{
			return this.InputPosInternal < Span.Length ? Span[this.InputPosInternal++] : '\0';
		}

		/// <summary>
		/// If we have a current token, then reset the pending comments and input position back to before the token.
		/// </summary>
		private void ClearToken()
		{
			if (this.bHasToken)
			{
				this.bHasToken = false;
				if (this.CommentsInternal != null && this.CommentsInternal.Count > this.CommittedComments)
				{
					this.CommentsInternal.RemoveRange(this.CommittedComments, this.CommentsInternal.Count - this.CommittedComments);
				}
				this.InputPosInternal = this.CurrentToken.UngetPos;
				this.InputLineInternal = this.CurrentToken.UngetLine;
			}
		}

		private void AddComment(StringView Comment)
		{
			if (this.CommentsInternal == null)
			{
				this.CommentsInternal = new List<StringView>(4);
			}
			this.CommentsInternal.Add(Comment);
		}
		#endregion
	}
}
