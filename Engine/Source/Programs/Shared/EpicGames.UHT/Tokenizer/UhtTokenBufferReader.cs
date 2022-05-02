// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using EpicGames.Core;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Tokenizer
{

	/// <summary>
	/// Token reader for source buffers
	/// </summary>
	public sealed class UhtTokenBufferReader : IUhtTokenReader, IUhtMessageLineNumber
	{
		private static readonly StringView[] s_emptyComments = Array.Empty<StringView>();

		private readonly IUhtMessageSite _messageSiteInternal;
		private List<StringView>? _commentsInternal = null;
		private readonly List<UhtToken> _recordedTokensInternal = new List<UhtToken>();
		private IUhtTokenPreprocessor? _tokenPreprocessorInternal = null;
		private UhtToken _currentToken = new UhtToken(); // PeekToken must have been invoked first
		private readonly ReadOnlyMemory<char> _data;
		private int _prevPos = 0;
		private int _prevLine = 1;
		private bool _hasToken = false;
		private int _preCurrentTokenInputPos = 0;
		private int _preCurrentTokenInputLine = 1;
		private int _inputPosInternal = 0;
		private int _inputLineInternal = 1;
		private int _commentsDisableCount = 0;
		private int _committedComments = 0;
		private List<StringView>? _savedComments = null;
		private int _savedInputPos = 0;
		private int _savedInputLine = 0;
		private bool _hasSavedState = false;
		private bool _recordTokens = false;
		private int _preprocessorPendingCommentsCount = 0;

		/// <summary>
		/// Construct a new token reader
		/// </summary>
		/// <param name="messageSite">Message site for messages</param>
		/// <param name="input">Input source</param>
		public UhtTokenBufferReader(IUhtMessageSite messageSite, ReadOnlyMemory<char> input)
		{
			this._messageSiteInternal = messageSite;
			this._data = input;
		}

		#region IUHTMessageSite Implementation
		IUhtMessageSession IUhtMessageSite.MessageSession => this._messageSiteInternal.MessageSession;
		IUhtMessageSource? IUhtMessageSite.MessageSource => this._messageSiteInternal.MessageSource;
		IUhtMessageLineNumber? IUhtMessageSite.MessageLineNumber => this;
		#endregion

		#region IUHTMessageLinenumber Implementation
		int IUhtMessageLineNumber.MessageLineNumber => this.InputLine;
		#endregion

		#region ITokenReader Implementation
		/// <inheritdoc/>
		public bool IsEOF
		{
			get
			{
				if (this._hasToken)
				{
					return this._currentToken.TokenType.IsEndType();
				}
				else
				{
					return this.InputPos == this._data.Length;
				}
			}
		}

		/// <inheritdoc/>
		public int InputPos => this._hasToken ? this._preCurrentTokenInputPos : this._inputPosInternal;

		/// <inheritdoc/>
		public int InputLine
		{
			get => this._hasToken ? this._preCurrentTokenInputLine : this._inputLineInternal;
			set
			{
				ClearToken();
				this._inputLineInternal = value;
			}
		}

		/// <inheritdoc/>
		public ReadOnlySpan<StringView> Comments
		{
			get
			{
				if (this._commentsInternal != null && this._committedComments != 0)
				{
					return new ReadOnlySpan<StringView>(this._commentsInternal.ToArray(), 0, this._committedComments);
				}
				else
				{
					return new ReadOnlySpan<StringView>(UhtTokenBufferReader.s_emptyComments);
				}
			}
		}

		/// <inheritdoc/>
		public IUhtTokenPreprocessor? TokenPreprocessor { get => this._tokenPreprocessorInternal; set => this._tokenPreprocessorInternal = value; }

		/// <inheritdoc/>
		public ref UhtToken PeekToken()
		{
			if (!this._hasToken)
			{
				this._currentToken = GetTokenInternal(true);
				this._hasToken = true;
			}
			return ref this._currentToken;
		}

		/// <inheritdoc/>
		public void SkipWhitespaceAndComments()
		{
			bool gotInlineComment = false;
			SkipWhitespaceAndCommentsInternal(ref gotInlineComment, true);
		}

		/// <inheritdoc/>
		public void ConsumeToken()
		{
			if (this._recordTokens && !this._currentToken.IsEndType())
			{
				this._recordedTokensInternal.Add(this._currentToken);
			}
			this._hasToken = false;

			// When comments are disabled, we are still collecting comments, but aren't committing them
			if (this._commentsDisableCount == 0)
			{
				if (this._commentsInternal != null)
				{
					this._committedComments = this._commentsInternal.Count;
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
			UhtToken token = PeekToken();
			ConsumeToken();
			return token;
		}

		/// <inheritdoc/>
		public StringView GetRawString(char terminator, UhtRawStringOptions options)
		{
			ReadOnlySpan<char> span = this._data.Span;

			ClearToken();
			SkipWhitespaceAndComments();

			int startPos = this.InputPos;
			bool inQuotes = false;
			while (true)
			{
				char c = InternalGetChar(span);

				// Check for end of file
				if (c == 0)
				{
					break;
				}

				// Check for end of line
				if (c == '\r' || c == '\n')
				{
					--this._inputPosInternal;
					break;
				}

				// Check for terminator as long as we aren't in quotes
				if (c == terminator && !inQuotes)
				{
					if (options.HasAnyFlags(UhtRawStringOptions.DontConsumeTerminator))
					{
						--this._inputPosInternal;
					}
					break;
				}

				// Check for comment
				if (!inQuotes && c == '/')
				{
					char p = InternalPeekChar(span);
					if (p == '*' || p == '/')
					{
						--this._inputPosInternal;
						break;
					}
				}

				// Check for quotes
				if (c == '"' && options.HasAnyFlags(UhtRawStringOptions.RespectQuotes))
				{
					inQuotes = !inQuotes;
				}
			}

			// If EOF, then error
			if (inQuotes)
			{
				throw new UhtException(this, "Unterminated quoted string");
			}

			// Remove trailing whitespace
			int endPos = this.InputPos;
			for (; endPos > startPos; --endPos)
			{
				char c = span[endPos - 1];
				if (c != ' ' && c != '\t')
				{
					break;
				}
			}

			// Check for too long
			if (endPos - startPos >= UhtToken.MaxStringLength)
			{
				throw new UhtException(this, $"String exceeds maximum of {UhtToken.MaxStringLength} characters");
			}
			return new StringView(this._data, startPos, endPos - startPos);
		}

		/// <inheritdoc/>
		public UhtToken GetLine()
		{
			ReadOnlySpan<char> span = this._data.Span;

			ClearToken();
			this._prevPos = this._inputPosInternal;
			this._prevLine = this._inputLineInternal;

			if (this._prevPos == span.Length)
			{
				return new UhtToken(UhtTokenType.EndOfFile, this._prevPos, this._prevLine, this._prevPos, this._prevLine, new StringView(this._data.Slice(this._prevPos, 0)));
			}

			int lastPos = this._inputPosInternal;
			while (true)
			{
				char c = InternalGetChar(span);
				if (c == 0)
				{
					break;
				}
				else if (c == '\r')
				{
				}
				else if (c == '\n')
				{
					++this._inputLineInternal;
					break;
				}
				else
				{
					lastPos = this._inputPosInternal;
				}
			}

			return new UhtToken(UhtTokenType.Line, this._prevPos, this._prevLine, this._prevPos, this._prevLine, new StringView(this._data.Slice(this._prevPos, lastPos - this._prevPos)));
		}

		/// <inheritdoc/>
		public StringView GetStringView(int startPos, int count)
		{
			return new StringView(this._data, startPos, count);
		}

		/// <inheritdoc/>
		public void ClearComments()
		{
			if (this._commentsInternal != null)
			{

				// Clearing comments does not remove any uncommitted comments
				this._commentsInternal.RemoveRange(0, this._committedComments);
				this._committedComments = 0;
			}
		}

		/// <inheritdoc/>
		public void DisableComments()
		{
			++this._commentsDisableCount;
		}

		/// <inheritdoc/>
		public void EnableComments()
		{
			--this._commentsDisableCount;
		}

		/// <inheritdoc/>
		public void CommitPendingComments()
		{
			if (this._commentsInternal != null)
			{
				this._committedComments = this._commentsInternal.Count;
			}
		}

		/// <inheritdoc/>
		public bool IsFirstTokenInLine(ref UhtToken token)
		{
			return IsFirstTokenInLine(this._data.Span, token.InputStartPos);
		}

		/// <inheritdoc/>
		public void SaveState()
		{
			if (this._hasSavedState)
			{
				throw new UhtIceException("Can not save more than one state");
			}
			this._hasSavedState = true;
			this._savedInputLine = this.InputLine;
			this._savedInputPos = this.InputPos;
			if (this._commentsInternal != null)
			{
				if (this._savedComments == null)
				{
					this._savedComments = new List<StringView>();
				}
				this._savedComments.Clear();
				this._savedComments.AddRange(this._commentsInternal);
			}
			if (this._tokenPreprocessorInternal != null)
			{
				this._tokenPreprocessorInternal.SaveState();
			}
		}

		/// <inheritdoc/>
		public void RestoreState()
		{
			if (!this._hasSavedState)
			{
				throw new UhtIceException("Can not restore state when none have been saved");
			}
			this._hasSavedState = false;
			ClearToken();
			this._inputPosInternal = this._savedInputPos;
			this._inputLineInternal = this._savedInputLine;
			if (this._savedComments != null && this._commentsInternal != null)
			{
				this._commentsInternal.Clear();
				this._commentsInternal.AddRange(this._savedComments);
			}
			if (this._tokenPreprocessorInternal != null)
			{
				this._tokenPreprocessorInternal.RestoreState();
			}
		}

		/// <inheritdoc/>
		public void AbandonState()
		{
			if (!this._hasSavedState)
			{
				throw new UhtIceException("Can not abandon state when none have been saved");
			}
			this._hasSavedState = false;
			ClearToken();
		}

		/// <inheritdoc/>
		public void EnableRecording()
		{
			if (this._recordTokens)
			{
				throw new UhtIceException("Can not nest token recording");
			}
			this._recordTokens = true;
		}

		/// <inheritdoc/>
		public void RecordToken(ref UhtToken token)
		{
			if (!this._recordTokens)
			{
				throw new UhtIceException("Attempt to disable token recording when it isn't enabled");
			}
			this._recordedTokensInternal.Add(token);
		}

		/// <inheritdoc/>
		public void DisableRecording()
		{
			if (!this._recordTokens)
			{
				throw new UhtIceException("Attempt to disable token recording when it isn't enabled");
			}
			this._recordedTokensInternal.Clear();
			this._recordTokens = false;
		}

		/// <inheritdoc/>
		public List<UhtToken> RecordedTokens
		{
			get
			{
				if (!this._recordTokens)
				{
					throw new UhtIceException("Attempt to get recorded tokens when it isn't enabled");
				}
				return this._recordedTokensInternal;
			}
		}
		#endregion

		#region Internals
		private void ClearAllComments()
		{
			if (this._commentsInternal != null)
			{
				this._commentsInternal.Clear();
				this._committedComments = 0;
			}
		}

		private void ClearPendingComments()
		{
			if (this._commentsInternal != null)
			{
				int startingComment = this._committedComments + this._preprocessorPendingCommentsCount;
				this._commentsInternal.RemoveRange(startingComment, this._commentsInternal.Count - startingComment);
			}
		}

		private static bool IsFirstTokenInLine(ReadOnlySpan<char> span, int startPos)
		{
			for (int pos = startPos; --pos > 0;)
			{
				switch (span[pos])
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
		private UhtToken GetTokenInternal(bool enablePreprocessor)
		{
			ReadOnlySpan<char> span = this._data.Span;
			bool gotInlineComment = false;
Restart:
			this._preCurrentTokenInputLine = this._inputLineInternal;
			this._preCurrentTokenInputPos = this._inputPosInternal;
			SkipWhitespaceAndCommentsInternal(ref gotInlineComment, enablePreprocessor);
			this._prevPos = this._inputPosInternal;
			this._prevLine = this._inputLineInternal;

			UhtTokenType tokenType = UhtTokenType.EndOfFile;
			int startPos = this._inputPosInternal;
			int startLine = this._inputLineInternal;

			char c = InternalGetChar(span);
			if (c == 0)
			{
			}

			else if (UhtFCString.IsAlpha(c) || c == '_')
			{

				for (; this._inputPosInternal < span.Length; ++this._inputPosInternal)
				{
					c = span[this._inputPosInternal];
					if (!(UhtFCString.IsAlpha(c) || UhtFCString.IsDigit(c) || c == '_'))
					{
						break;
					}
				}

				if (this._inputPosInternal - startPos >= UhtToken.MaxNameLength)
				{
					throw new UhtException(this, $"Identifier length exceeds maximum of {UhtToken.MaxNameLength}");
				}

				tokenType = UhtTokenType.Identifier;
			}

			// Check for any numerics 
			else if (IsNumeric(span, c))
			{
				// Integer or floating point constant.

				bool isFloat = c == '.';
				bool isHex = false;

				// Ignore the starting sign for this part of the parsing
				if (UhtFCString.IsSign(c))
				{
					c = InternalGetChar(span); // We know this won't fail since the code above checked the Peek
				}

				// Check for a hex valid
				if (c == '0')
				{
					isHex = UhtFCString.IsHexMarker(InternalPeekChar(span));
					if (isHex)
					{
						InternalGetChar(span);
					}
				}

				// If we have a hex constant
				if (isHex)
				{
					for (; this._inputPosInternal < span.Length && UhtFCString.IsHexDigit(span[this._inputPosInternal]); ++this._inputPosInternal)
					{
					}
				}

				// We have decimal/octal value or possibly a floating point value
				else
				{

					// Skip all digits
					for (; this._inputPosInternal < span.Length && UhtFCString.IsDigit(span[this._inputPosInternal]); ++this._inputPosInternal)
					{
					}

					// If we have a '.'
					if (this._inputPosInternal < span.Length && span[this._inputPosInternal] == '.')
					{
						isFloat = true;
						++this._inputPosInternal;

						// Skip all digits
						for (; this._inputPosInternal < span.Length && UhtFCString.IsDigit(span[this._inputPosInternal]); ++this._inputPosInternal)
						{
						}
					}

					// If we have a 'e'
					if (this._inputPosInternal < span.Length && UhtFCString.IsExponentMarker(span[this._inputPosInternal]))
					{
						isFloat = true;
						++this._inputPosInternal;

						// Skip any signs
						if (this._inputPosInternal < span.Length && UhtFCString.IsSign(span[this._inputPosInternal]))
						{
							++this._inputPosInternal;
						}

						// Skip all digits
						for (; this._inputPosInternal < span.Length && UhtFCString.IsDigit(span[this._inputPosInternal]); ++this._inputPosInternal)
						{
						}
					}

					// If we have a 'f'
					if (this._inputPosInternal < span.Length && UhtFCString.IsFloatMarker(span[this._inputPosInternal]))
					{
						isFloat = true;
						++this._inputPosInternal;
					}

					// Check for u/l markers
					while (this._inputPosInternal < span.Length &&
						(UhtFCString.IsUnsignedMarker(span[this._inputPosInternal]) || UhtFCString.IsLongMarker(span[this._inputPosInternal])))
					{
						++this._inputPosInternal;
					}
				}

				if (this._inputPosInternal - startPos >= UhtToken.MaxNameLength)
				{
					throw new UhtException(this, $"Number length exceeds maximum of {UhtToken.MaxNameLength}");
				}

				tokenType = isFloat ? UhtTokenType.FloatConst : (isHex ? UhtTokenType.HexConst : UhtTokenType.DecimalConst);
			}

			// Escaped character constant
			else if (c == '\'')
			{

				// We try to skip the character constant value. But if it is backslash, we have to skip another character
				if (InternalGetChar(span) == '\\')
				{
					InternalGetChar(span);
				}

				char nextChar = InternalGetChar(span);
				if (nextChar != '\'')
				{
					throw new UhtException(this, "Unterminated character constant");
				}

				tokenType = UhtTokenType.CharConst;
			}

			// String constant
			else if (c == '"')
			{
				for (; ; )
				{
					char nextChar = InternalGetChar(span);
					if (nextChar == '\r' || nextChar == '\n')
					{
						// throw
					}

					if (nextChar == '\\')
					{
						nextChar = InternalGetChar(span);
						if (nextChar == '\r' || nextChar == '\n')
						{
							// throw
						}
						nextChar = InternalGetChar(span);
					}

					if (nextChar == '"')
					{
						break;
					}
				}

				if (this._inputPosInternal - startPos >= UhtToken.MaxStringLength)
				{
					throw new UhtException(this, $"String constant exceeds maximum of {UhtToken.MaxStringLength} characters");
				}

				tokenType = UhtTokenType.StringConst;
			}

			// Assume everything else is a symbol.
			// Don't handle >> or >>>
			else
			{
				{
					if (this._inputPosInternal < span.Length)
					{
						char d = span[this._inputPosInternal];
						if ((c == '<' && d == '<')
							|| (c == '!' && d == '=')
							|| (c == '<' && d == '=')
							|| (c == '>' && d == '=')
							|| (c == '+' && d == '+')
							|| (c == '-' && d == '-')
							|| (c == '+' && d == '=')
							|| (c == '-' && d == '=')
							|| (c == '*' && d == '=')
							|| (c == '/' && d == '=')
							|| (c == '&' && d == '&')
							|| (c == '|' && d == '|')
							|| (c == '^' && d == '^')
							|| (c == '=' && d == '=')
							|| (c == '*' && d == '*')
							|| (c == '~' && d == '=')
							|| (c == ':' && d == ':'))
						{
							++this._inputPosInternal;
						}
					}

					// Comment processing while processing the preprocessor statements is complicated.  When a preprocessor statement
					// parsers, the tokenizer is doing some form of a PeekToken.  And comments read ahead of the '#' will still be 
					// pending.
					//
					// 1) If the block is being skipped, we want to preserve any comments ahead of the token and eliminate all other comments found in the #if block
					// 2) If the block isn't being skipped or in the case of things such as #pragma or #include, they were considered statements in the UHT
					//	  and would result in the comments being dropped.  In the new UHT, we just eliminate all pending tokens.
					if (c == '#' && enablePreprocessor && this._tokenPreprocessorInternal != null && IsFirstTokenInLine(span, startPos))
					{
						this._preprocessorPendingCommentsCount = this._commentsInternal != null ? this._commentsInternal.Count - this._committedComments : 0;
						UhtToken temp = new UhtToken(tokenType, startPos, startLine, startPos, this._inputLineInternal, new StringView(this._data.Slice(startPos, this._inputPosInternal - startPos)));
						bool include = this._tokenPreprocessorInternal.ParsePreprocessorDirective(ref temp, true, out bool clearComments, out bool illegalContentsCheck);
						if (!include)
						{
							++this._commentsDisableCount;
							int checkStateMachine = 0;
							while (true)
							{
								if (IsEOF)
								{
									break;
								}
								UhtToken localToken = GetTokenInternal(false);
								ReadOnlySpan<char> localValueSpan = localToken.Value.Span;
								if (localToken.IsSymbol('#') && localValueSpan.Length == 1 && localValueSpan[0] == '#' && IsFirstTokenInLine(span, localToken.InputStartPos))
								{
									if (this._tokenPreprocessorInternal.ParsePreprocessorDirective(ref temp, false, out bool _, out bool scratchIllegalContentsCheck))
									{
										break;
									}
									illegalContentsCheck = scratchIllegalContentsCheck;
								}
								else if (illegalContentsCheck)
								{
									switch (checkStateMachine)
									{
										case 0:
ResetStateMachineCheck:
											if (localValueSpan.CompareTo("UPROPERTY", StringComparison.Ordinal) == 0 ||
												localValueSpan.CompareTo("UCLASS", StringComparison.Ordinal) == 0 ||
												localValueSpan.CompareTo("USTRUCT", StringComparison.Ordinal) == 0 ||
												localValueSpan.CompareTo("UENUM", StringComparison.Ordinal) == 0 ||
												localValueSpan.CompareTo("UINTERFACE", StringComparison.Ordinal) == 0 ||
												localValueSpan.CompareTo("UDELEGATE", StringComparison.Ordinal) == 0 ||
												localValueSpan.CompareTo("UFUNCTION", StringComparison.Ordinal) == 0)
											{
												this.LogError($"'{localValueSpan.ToString()}' must not be inside preprocessor blocks, except for WITH_EDITORONLY_DATA");
											}
											else if (localValueSpan.CompareTo("void", StringComparison.Ordinal) == 0)
											{
												checkStateMachine = 1;
											}
											break;

										case 1:
											if (localValueSpan.CompareTo("Serialize", StringComparison.Ordinal) == 0)
											{
												checkStateMachine = 2;
											}
											else
											{
												checkStateMachine = 0;
												goto ResetStateMachineCheck;
											}
											break;

										case 2:
											if (localValueSpan.CompareTo("(", StringComparison.Ordinal) == 0)
											{
												checkStateMachine = 3;
											}
											else
											{
												checkStateMachine = 0;
												goto ResetStateMachineCheck;
											}
											break;

										case 3:
											if (localValueSpan.CompareTo("FArchive", StringComparison.Ordinal) == 0 ||
												localValueSpan.CompareTo("FStructuredArchiveRecord", StringComparison.Ordinal) == 0)
											{
												this.LogError($"Engine serialization functions must not be inside preprocessor blocks, except for WITH_EDITORONLY_DATA");
												checkStateMachine = 0;
											}
											else if (localValueSpan.CompareTo("FStructuredArchive", StringComparison.Ordinal) == 0)
											{
												checkStateMachine = 4;
											}
											else
											{
												checkStateMachine = 0;
												goto ResetStateMachineCheck;
											}
											break;

										case 4:
											if (localValueSpan.CompareTo("::", StringComparison.Ordinal) == 0)
											{
												checkStateMachine = 5;
											}
											else
											{
												checkStateMachine = 0;
												goto ResetStateMachineCheck;
											}
											break;

										case 5:
											if (localValueSpan.CompareTo("FRecord", StringComparison.Ordinal) == 0)
											{
												this.LogError($"Engine serialization functions must not be inside preprocessor blocks, except for WITH_EDITORONLY_DATA");
												checkStateMachine = 0;
											}
											else
											{
												checkStateMachine = 0;
												goto ResetStateMachineCheck;
											}
											break;
									}
								}
							}
							--this._commentsDisableCount;

							// Clear any extra pending comments at this time
							ClearPendingComments();
						}
						this._preprocessorPendingCommentsCount = 0;

						// Depending on the type of directive and/or the #if expression, we clear comments.
						if (clearComments)
						{
							ClearPendingComments();
							gotInlineComment = false;
						}
						goto Restart;
					}
				}
				tokenType = UhtTokenType.Symbol;
			}

			return new UhtToken(tokenType, startPos, startLine, startPos, this._inputLineInternal, new StringView(this._data.Slice(startPos, this._inputPosInternal - startPos)));
		}

		/// <summary>
		/// Skip all leading whitespace and collect any comments
		/// </summary>
		private void SkipWhitespaceAndCommentsInternal(ref bool persistGotInlineComment, bool enablePreprocessor)
		{
			ReadOnlySpan<char> span = this._data.Span;

			bool gotNewlineBetweenComments = false;
			bool gotInlineComment = persistGotInlineComment;
			for (; ; )
			{
				uint c = InternalGetChar(span);
				if (c == 0)
				{
					break;
				}
				else if (c == '\n')
				{
					gotNewlineBetweenComments |= gotInlineComment;
					++this._inputLineInternal;
				}
				else if (c == '\r' || c == '\t' || c == ' ')
				{
				}
				else if (c == '/')
				{
					uint nextChar = InternalPeekChar(span);
					if (nextChar == '*')
					{
						if (enablePreprocessor)
						{
							ClearAllComments();
						}
						int commentStart = this._inputPosInternal - 1;
						++this._inputPosInternal;
						for (; ; )
						{
							char commentChar = InternalGetChar(span);
							if (commentChar == 0)
							{
								if (enablePreprocessor)
								{
									ClearAllComments();
								}
								throw new UhtException(this, "End of header encountered inside comment");
							}
							else if (commentChar == '\n')
							{
								++this._inputLineInternal;
							}
							else if (commentChar == '*' && InternalPeekChar(span) == '/')
							{
								++this._inputPosInternal;
								break;
							}
						}
						if (enablePreprocessor)
						{
							AddComment(new StringView(this._data.Slice(commentStart, this._inputPosInternal - commentStart)));
						}
					}
					else if (nextChar == '/')
					{
						if (gotNewlineBetweenComments)
						{
							gotNewlineBetweenComments = false;
							if (enablePreprocessor)
							{
								ClearAllComments();
							}
						}
						gotInlineComment = true;
						int commentStart = this._inputPosInternal - 1;
						++this._inputPosInternal;

						// Scan to the end of the line
						for (; ; )
						{
							char commentChar = InternalGetChar(span);
							if (commentChar == 0)
							{
								//--Pos;
								break;
							}
							if (commentChar == '\r')
							{
							}
							else if (commentChar == '\n')
							{
								++_inputLineInternal;
								break;
							}
						}
						if (enablePreprocessor)
						{
							AddComment(new StringView(this._data.Slice(commentStart, this._inputPosInternal - commentStart)));
						}
					}
					else
					{
						--this._inputPosInternal;
						break;
					}
				}
				else
				{
					--this._inputPosInternal;
					break;
				}
			}
			persistGotInlineComment = gotInlineComment;
			return;
		}

		private bool IsNumeric(ReadOnlySpan<char> span, char c)
		{
			// Check for [0..9]
			if (UhtFCString.IsDigit(c))
			{
				return true;
			}

			// Check for [+-]...
			if (UhtFCString.IsSign(c))
			{
				if (this._inputPosInternal == span.Length)
				{
					return false;
				}

				// Check for [+-][0..9]...
				if (UhtFCString.IsDigit(span[this._inputPosInternal]))
				{
					return true;
				}

				// Check for [+-][.][0..9]...
				if (span[this._inputPosInternal] != '.')
				{
					return false;
				}

				if (this._inputPosInternal + 1 == span.Length)
				{
					return false;
				}

				if (UhtFCString.IsDigit(span[this._inputPosInternal + 1]))
				{
					return true;
				}
				return false;
			}

			if (c == '.')
			{
				if (this._inputPosInternal == span.Length)
				{
					return false;
				}

				// Check for [.][0..9]...
				if (UhtFCString.IsDigit(span[this._inputPosInternal]))
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
		/// <param name="span">The span containing the data</param>
		/// <returns>Next character in the stream or zero</returns>
		private char InternalPeekChar(ReadOnlySpan<char> span)
		{
			return this._inputPosInternal < span.Length ? span[this._inputPosInternal] : '\0';
		}

		/// <summary>
		/// Fetch the next character in the input stream or zero if we have reached the end.
		/// The current offset in the buffer is advanced.  The method does not support UTF-8.
		/// </summary>
		/// <param name="span">The span containing the data</param>
		/// <returns>Next character in the stream or zero</returns>
		private char InternalGetChar(ReadOnlySpan<char> span)
		{
			return this._inputPosInternal < span.Length ? span[this._inputPosInternal++] : '\0';
		}

		/// <summary>
		/// If we have a current token, then reset the pending comments and input position back to before the token.
		/// </summary>
		private void ClearToken()
		{
			if (this._hasToken)
			{
				this._hasToken = false;
				if (this._commentsInternal != null && this._commentsInternal.Count > this._committedComments)
				{
					this._commentsInternal.RemoveRange(this._committedComments, this._commentsInternal.Count - this._committedComments);
				}
				this._inputPosInternal = this._currentToken.UngetPos;
				this._inputLineInternal = this._currentToken.UngetLine;
			}
		}

		private void AddComment(StringView comment)
		{
			if (this._commentsInternal == null)
			{
				this._commentsInternal = new List<StringView>(4);
			}
			this._commentsInternal.Add(comment);
		}
		#endregion
	}
}
