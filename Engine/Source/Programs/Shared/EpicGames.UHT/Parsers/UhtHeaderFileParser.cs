// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.IO;

namespace EpicGames.UHT.Parsers
{
	public enum UhtParseResult
	{
		Handled,
		Unhandled,
		Invalid,
	}

	[Flags]
	public enum UhtCompilerDirective
	{
		None = 0,
		/// <summary>
		/// This indicates we are in a "#if CPP" block
		/// </summary>
		CPPBlock = 1 << 0,
		/// <summary>
		/// This indicates we are in a "#if !CPP" block
		/// </summary>
		NotCPPBlock = 1 << 1,
		/// <summary>
		/// This indicates we are in a "#if 0" block
		/// </summary>
		ZeroBlock = 1 << 2,
		/// <summary>
		/// This indicates we are in a "#if 1" block
		/// </summary>
		OneBlock = 1 << 3,
		/// <summary>
		/// This indicates we are in a "#if WITH_EDITOR" block
		/// </summary>
		WithEditor = 1 << 4,
		/// <summary>
		/// This indicates we are in a "#if WITH_EDITORONLY_DATA" block
		/// </summary>
		WithEditorOnlyData = 1 << 5,
		/// <summary>
		/// This indicates we are in a "#if WITH_HOT_RELOAD" block
		/// </summary>
		WithHotReload = 1 << 6,
		/// <summary>
		/// This directive is unrecognized and does not change the code generation at all
		/// </summary>
		Unrecognized = 1 << 7,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtCompilerDirectiveExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtCompilerDirective InFlags, UhtCompilerDirective TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtCompilerDirective InFlags, UhtCompilerDirective TestFlags)
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
		public static bool HasExactFlags(this UhtCompilerDirective InFlags, UhtCompilerDirective TestFlags, UhtCompilerDirective MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	[UnrealHeaderTool]
	public static class UhtAccessSpecifierKeywords
	{
		#region Keywords
		[UhtKeyword(Extends = UhtTableNames.ClassBase, Keyword = "public")]
		[UhtKeyword(Extends = UhtTableNames.ScriptStruct, Keyword = "public")]
		private static UhtParseResult PublicKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			return SetAccessSpecifier(TopScope, UhtAccessSpecifier.Public);
		}

		[UhtKeyword(Extends = UhtTableNames.ClassBase, Keyword = "protected")]
		[UhtKeyword(Extends = UhtTableNames.ScriptStruct, Keyword = "protected")]
		private static UhtParseResult ProtectedKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			return SetAccessSpecifier(TopScope, UhtAccessSpecifier.Protected);
		}

		[UhtKeyword(Extends = UhtTableNames.ClassBase, Keyword = "private")]
		[UhtKeyword(Extends = UhtTableNames.ScriptStruct, Keyword = "private")]
		private static UhtParseResult PrivateKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			return SetAccessSpecifier(TopScope, UhtAccessSpecifier.Private);
		}
		#endregion

		private static UhtParseResult SetAccessSpecifier(UhtParsingScope TopScope, UhtAccessSpecifier AccessSpecifier)
		{
			TopScope.AccessSpecifier = AccessSpecifier;
			TopScope.TokenReader.Require(':');
			return UhtParseResult.Handled;
		}
	}

	public class UhtHeaderFileParser : IUhtTokenPreprocessor
	{
		private static UhtKeywordTable KeywordTable = UhtKeywordTables.Instance.Get(UhtTableNames.Global);

		private struct CompilerDirective
		{
			public UhtCompilerDirective Element;
			public UhtCompilerDirective Composite;
		}

		/// <summary>
		/// Header file being parsed
		/// </summary>
		public readonly UhtHeaderFile HeaderFile;

		/// <summary>
		/// Token reader for the header
		/// </summary>
		public readonly IUhtTokenReader TokenReader;

		/// <summary>
		/// If true, this header file belongs to the engine
		/// </summary>
		public bool bIsPartOfEngine => this.HeaderFile.Package.bIsPartOfEngine;

		/// <summary>
		/// If true, the inclusion of the generated header file was seen
		/// </summary>
		public bool bSpottedAutogeneratedHeaderInclude = false;

		/// <summary>
		/// For a given header file, we share a common specifier parser to reduce the number of allocations.
		/// Before the parser can be reused, the ParseDeferred method must be called to dispatch that list.
		/// </summary>
		private UhtSpecifierParser? SpecifierParser = null;

		/// <summary>
		/// For a given header file, we share a common property parser to reduce the number of allocations.
		/// </summary>
		public UhtPropertyParser? PropertyParser = null;

		/// <summary>
		/// Stack of current #if states
		/// </summary>
		private List<CompilerDirective> CompilerDirectives = new List<CompilerDirective>();

		/// <summary>
		/// Stack of current #if states saved as part of the preprocessor state
		/// </summary>
		private List<CompilerDirective> SavedCompilerDirectives = new List<CompilerDirective>();

		/// <summary>
		/// Current top of the parsing scopes.  Classes, structures and functions all allocate scopes.
		/// </summary>
		private UhtParsingScope? TopScope = null;

		/// <summary>
		/// The total number of statements parsed
		/// </summary>
		private int StatementsParsed = 0;

		public static UhtHeaderFileParser Parse(UhtHeaderFile HeaderFile)
		{
			UhtHeaderFileParser HeaderParser = new UhtHeaderFileParser(HeaderFile);
			using (UhtParsingScope TopScope = new UhtParsingScope(HeaderParser, HeaderParser.HeaderFile, UhtHeaderFileParser.KeywordTable))
			{
				HeaderParser.ParseStatements();

				if (!HeaderParser.bSpottedAutogeneratedHeaderInclude && HeaderParser.HeaderFile.Data.Length > 0)
				{
					bool bNoExportClassesOnly = true;
					foreach (UhtType Type in HeaderParser.HeaderFile.Children)
					{
						if (Type is UhtClass Class)
						{
							if (Class.ClassType != UhtClassType.NativeInterface && !Class.ClassFlags.HasAnyFlags(EClassFlags.NoExport))
							{
								bNoExportClassesOnly = false;
								break;
							}
						}
					}

					if (!bNoExportClassesOnly)
					{
						HeaderParser.HeaderFile.LogError($"Expected an include at the top of the header the follows all other includes: '#include \"{HeaderParser.HeaderFile.GeneratedHeaderFileName}\"'");
					}
				}
			}
			return HeaderParser;
		}

		public UhtHeaderFileParser(UhtHeaderFile HeaderFile)
		{
			this.TokenReader = new UhtTokenBufferReader(HeaderFile, HeaderFile.Data.Memory);
			this.HeaderFile = HeaderFile;
			this.TokenReader.TokenPreprocessor = this;
		}

		public void PushScope(UhtParsingScope Scope)
		{
			if (Scope.ParentScope != this.TopScope)
			{
				throw new UhtIceException("Pushing a new scope whose parent isn't the current top scope.");
			}
			this.TopScope = Scope;
		}

		public void PopScope(UhtParsingScope Scope)
		{
			if (Scope != this.TopScope)
			{
				throw new UhtIceException("Attempt to pop a scope that isn't the top scope");
			}
			this.TopScope = Scope.ParentScope;
		}

		public UhtSpecifierParser GetSpecifierParser(UhtSpecifierContext SpecifierContext, StringView Context, UhtSpecifierTable Table)
		{
			if (this.SpecifierParser == null)
			{
				this.SpecifierParser = new UhtSpecifierParser(SpecifierContext, Context, Table);
			}
			else
			{
				this.SpecifierParser.Reset(SpecifierContext, Context, Table);
			}
			return this.SpecifierParser;
		}

		public UhtPropertyParser GetPropertyParser(UhtParsingScope TopScope)
		{
			if (this.PropertyParser == null)
			{
				this.PropertyParser = new UhtPropertyParser(TopScope, TopScope.TokenReader);
			}
			else
			{
				this.PropertyParser.Reset(TopScope, TopScope.TokenReader);
			}
			return this.PropertyParser;
		}

		/// <summary>
		/// Return the current compiler directive
		/// </summary>
		/// <returns>Enumeration flags for all active compiler directives</returns>
		public UhtCompilerDirective GetCurrentCompositeCompilerDirective()
		{
			return this.CompilerDirectives.Count > 0 ? this.CompilerDirectives[this.CompilerDirectives.Count - 1].Composite : UhtCompilerDirective.None;
		}

		public UhtCompilerDirective GetCurrentNonCompositeCompilerDirective()
		{
			return this.CompilerDirectives.Count > 0 ? this.CompilerDirectives[this.CompilerDirectives.Count - 1].Element : UhtCompilerDirective.None;
		}

		#region ITokenPreprocessor implementation
		public bool ParsePreprocessorDirective(ref UhtToken Token, bool bIsBeingIncluded, out bool bClearComments, out bool bIllegalContentsCheck)
		{
			bClearComments = true;
			bIllegalContentsCheck = true;
			if (ParseDirectiveInternal(Token, bIsBeingIncluded))
			{
				bClearComments = ClearCommentsCompilerDirective();
			}
			bIllegalContentsCheck = !GetCurrentNonCompositeCompilerDirective().HasAnyFlags(UhtCompilerDirective.ZeroBlock | UhtCompilerDirective.WithEditorOnlyData);
			return IncludeCurrentCompilerDirective();
		}

		public void SaveState()
		{
			this.SavedCompilerDirectives.Clear();
			this.SavedCompilerDirectives.AddRange(this.CompilerDirectives);
		}

		public void RestoreState()
		{
			this.CompilerDirectives.Clear();
			this.CompilerDirectives.AddRange(this.SavedCompilerDirectives);
		}
		#endregion

		#region Statement parsing
		public void ParseStatements()
		{
			ParseStatements((char)0, (char)0, true);
		}

		public void ParseStatements(char Initiator, char Terminator, bool bLogUnhandledKeywords)
		{
			if (this.TopScope == null)
			{
				return;
			}

			if (Initiator != 0)
			{
				this.TokenReader.Require(Initiator);
			}
			while (true)
			{
				UhtToken Token = this.TokenReader.GetToken();
				if (Token.TokenType.IsEndType())
				{
					if (this.TopScope != null && this.TopScope.ParentScope == null)
					{
						CheckEof(ref Token);
					}
					return;
				}
				else if (Terminator != 0 && Token.IsSymbol(Terminator))
				{
					return;
				}

				if (this.TopScope != null)
				{
					ParseStatement(this.TopScope, ref Token, bLogUnhandledKeywords);
					this.TokenReader.ClearComments();
					++this.StatementsParsed;
				}
			}
		}


		public static bool ParseStatement(UhtParsingScope TopScope, ref UhtToken Token, bool bLogUnhandledKeywords)
		{
			UhtParseResult ParseResult = UhtParseResult.Unhandled;

			switch (Token.TokenType)
			{
				case UhtTokenType.Identifier:
					ParseResult = DispatchKeyword(TopScope, ref Token);
					break;

				case UhtTokenType.Symbol:
					// Ignore any extra semicolons
					if (Token.IsSymbol(';'))
					{
						return true;
					}
					ParseResult = DispatchKeyword(TopScope, ref Token);
					break;
			}

			if (ParseResult == UhtParseResult.Unhandled)
			{
				ParseResult = DispatchCatchAll(TopScope, ref Token);
			}

			if (ParseResult == UhtParseResult.Unhandled)
			{
				ParseResult = ProbablyAnUnknownObjectLikeMacro(TopScope.TokenReader, ref Token);
			}

			if (ParseResult == UhtParseResult.Unhandled && bLogUnhandledKeywords)
			{
				UhtKeywordTables.Instance.LogUnhandledError(TopScope.TokenReader, Token);
			}

			if (ParseResult == UhtParseResult.Unhandled || ParseResult == UhtParseResult.Invalid)
			{
				if (TopScope.ScopeType is UhtClass Class)
				{
					using (UhtTokenRecorder Recorder = new UhtTokenRecorder(TopScope, ref Token))
					{
						ParseResult = TopScope.TokenReader.SkipDeclaration(ref Token) ? UhtParseResult.Handled : UhtParseResult.Invalid;
						if (Recorder.Stop())
						{
							if (Class.Declarations != null)
							{
								UhtDeclaration Declaration = Class.Declarations[Class.Declarations.Count - 1];
								if (TopScope.HeaderParser.CheckForConstructor(Class, Declaration))
								{
								}
								else if (Class.ClassType == UhtClassType.Class)
								{
									if (TopScope.HeaderParser.CheckForSerialize(Class, Declaration))
									{
									}
								}
							}
						}
					}
				}
				else
				{
					ParseResult = TopScope.TokenReader.SkipDeclaration(ref Token) ? UhtParseResult.Handled : UhtParseResult.Invalid;
				}
			}
			return true;
		}

		private static UhtParseResult DispatchKeyword(UhtParsingScope TopScope, ref UhtToken Token)
		{
			UhtParseResult ParseResult = UhtParseResult.Unhandled;
			for (UhtParsingScope? CurrentScope = TopScope; CurrentScope != null && ParseResult == UhtParseResult.Unhandled; CurrentScope = CurrentScope.ParentScope)
			{
				UhtKeyword KeywordInfo;
				if (CurrentScope.ScopeKeywordTable.TryGetValue(Token.Value, out KeywordInfo))
				{
					if (KeywordInfo.bAllScopes || TopScope == CurrentScope)
					{
						ParseResult = KeywordInfo.Delegate(TopScope, CurrentScope, ref Token);
					}
				}
			}
			return ParseResult;
		}

		private static UhtParseResult DispatchCatchAll(UhtParsingScope TopScope, ref UhtToken Token)
		{
			for (UhtParsingScope? CurrentScope = TopScope; CurrentScope != null; CurrentScope = CurrentScope.ParentScope)
			{
				foreach (var Delegate in CurrentScope.ScopeKeywordTable.CatchAlls)
				{
					UhtParseResult ParseResult = Delegate(TopScope, ref Token);
					if (ParseResult != UhtParseResult.Unhandled)
					{
						return ParseResult;
					}
				}
			}
			return UhtParseResult.Unhandled;
		}

		/// <summary>
		/// Tests if an identifier looks like a macro which doesn't have a following open parenthesis.
		/// </summary>
		/// <param name="Token">The current token that initiated the process</param>
		/// <returns>Result if matching the token</returns>
		private static UhtParseResult ProbablyAnUnknownObjectLikeMacro(IUhtTokenReader TokenReader, ref UhtToken Token)
		{
			// Non-identifiers are not macros
			if (!Token.IsIdentifier())
			{
				return UhtParseResult.Unhandled;
			}

			// Macros must start with a capitalized alphanumeric character or underscore
			char FirstChar = Token.Value.Span[0];
			if (FirstChar != '_' && (FirstChar < 'A' || FirstChar > 'Z'))
			{
				return UhtParseResult.Unhandled;
			}

			// We'll guess about it being a macro based on it being fully-capitalized with at least one underscore.
			int UnderscoreCount = 0;
			foreach (char Ch in Token.Value.Span.Slice(1))
			{
				if (Ch == '_')
				{
					++UnderscoreCount;
				}
				else if ((Ch < 'A' || Ch > 'Z') && (Ch < '0' || Ch > '9'))
				{
					return UhtParseResult.Unhandled;
				}
			}

			// We look for at least one underscore as a convenient way of allowing many known macros
			// like FORCEINLINE and CONSTEXPR, and non-macros like FPOV and TCHAR.
			if (UnderscoreCount == 0)
			{
				return UhtParseResult.Unhandled;
			}

			// Identifiers which end in _API are known
			if (Token.Value.Span.Length > 4 && Token.Value.Span.EndsWith("_API"))
			{
				return UhtParseResult.Unhandled;
			}

			// Ignore certain known macros or identifiers that look like macros.
			if (Token.IsValue("FORCEINLINE_DEBUGGABLE") ||
				Token.IsValue("FORCEINLINE_STATS") ||
				Token.IsValue("SIZE_T"))
			{
				return UhtParseResult.Unhandled;
			}

			// Check if there's an open parenthesis following the token.
			return TokenReader.PeekToken().IsSymbol('(') ? UhtParseResult.Unhandled : UhtParseResult.Handled;
		}

		private void CheckEof(ref UhtToken Token)
		{
			if (this.CompilerDirectives.Count > 0)
			{
				throw new UhtException(this.TokenReader, Token.InputLine, "Missing #endif");
			}
		}
		#endregion

		#region Internals
		/// <summary>
		/// Parse a preprocessor directive.
		/// </summary>
		/// <param name="Token">Token that started the parse</param>
		/// <param name="bIsBeingIncluded">If true, then this directive is in an active block</param>
		/// <returns>True if we should check to see if tokenizer should clear comments</returns>
		private bool ParseDirectiveInternal(UhtToken Token, bool bIsBeingIncluded)
		{
			bool bCheckClearComments = false;

			// Collect all the lines of the preprocessor statement including any continuations.
			// We assume that the vast majority of lines will not be continuations.  So avoid using the
			// string builder as much as possible.
			int StartingLine = this.TokenReader.InputLine;
			StringViewBuilder SVB = new StringViewBuilder();
			while (true)
			{
				UhtToken LineToken = this.TokenReader.GetLine();
				if (LineToken.TokenType != UhtTokenType.Line)
				{
					break;
				}
				if (LineToken.Value.Span.Length > 0 && LineToken.Value.Span[LineToken.Value.Span.Length - 1] == '\\')
				{
					SVB.Append(new StringView(LineToken.Value, 0, LineToken.Value.Span.Length - 1));
				}
				else
				{
					SVB.Append(LineToken.Value);
					break;
				}
			}
			StringView Line = SVB.ToStringView();

			// Create a token reader we will use to decode
			UhtTokenBufferReader LineTokenReader = new UhtTokenBufferReader(this.HeaderFile, Line.Memory);
			LineTokenReader.InputLine = StartingLine;

			UhtToken Directive;
			if (!LineTokenReader.TryOptionalIdentifier(out Directive))
			{
				if (bIsBeingIncluded)
				{
					throw new UhtException(this.TokenReader, Directive.InputLine, "Missing compiler directive after '#'");
				}
				return bCheckClearComments;
			}

			if (Directive.IsValue("error"))
			{
				if (bIsBeingIncluded)
				{
					throw new UhtException(this.TokenReader, Directive.InputLine, "#error directive encountered");
				}
			}
			else if (Directive.IsValue("pragma"))
			{
				// Ignore all pragmas
			}
			else if (Directive.IsValue("linenumber"))
			{
				int NewInputLine;
				if (!LineTokenReader.TryOptionalConstInt(out NewInputLine))
				{
					throw new UhtException(this.TokenReader, Directive.InputLine, "Missing line number in line number directive");
				}
				this.TokenReader.InputLine = NewInputLine;
			}
			else if (Directive.IsValue("include"))
			{
				if (bIsBeingIncluded)
				{
					UhtToken IncludeName = LineTokenReader.GetToken();
					if (IncludeName.IsConstString())
					{
						StringView IncludeNameString = IncludeName.GetUnescapedString(this.HeaderFile);
						if (IncludeNameString != "UObject/DefineUPropertyMacros.h" && IncludeNameString != "UObject/UndefineUPropertyMacros.h")
						{
							if (this.bSpottedAutogeneratedHeaderInclude)
							{
								this.HeaderFile.LogError("#include found after .generated.h file - the .generated.h file should always be the last #include in a header");
							}
							if (this.HeaderFile.GeneratedHeaderFileName.AsSpan().Equals(IncludeNameString.Span, StringComparison.OrdinalIgnoreCase))
							{
								this.bSpottedAutogeneratedHeaderInclude = true;
							}
							if (IncludeNameString.Span.Contains(".generated.h", StringComparison.Ordinal))
							{
								string RefName = Path.GetFileName(IncludeNameString.ToString()).Replace(".generated.h", ".h");
								this.HeaderFile.AddReferencedHeader(RefName, true);
							}
							else
							{
								this.HeaderFile.AddReferencedHeader(IncludeNameString.ToString(), true);
							}
						}
					}
				}
			}
			else if (Directive.IsValue("if"))
			{
				bCheckClearComments = true;
				PushCompilerDirective(ParserConditional(LineTokenReader));
			}
			else if (Directive.IsValue("ifdef") || Directive.IsValue("ifndef"))
			{
				bCheckClearComments = true;
				PushCompilerDirective(UhtCompilerDirective.Unrecognized);
			}
			else if (Directive.IsValue("elif"))
			{
				bCheckClearComments = true;
				UhtCompilerDirective OldCompilerDirective = PopCompilerDirective(Directive);
				UhtCompilerDirective NewCompilerDirective = ParserConditional(LineTokenReader);
				if (SupportsElif(OldCompilerDirective) != SupportsElif(NewCompilerDirective))
				{
					throw new UhtException(this.TokenReader, Directive.InputLine, 
						$"Mixing {GetCompilerDirectiveText(OldCompilerDirective)} with {GetCompilerDirectiveText(NewCompilerDirective)} in an #elif preprocessor block is not supported");
				}
				PushCompilerDirective(NewCompilerDirective);
			}
			else if (Directive.IsValue("else"))
			{
				bCheckClearComments = true;
				UhtCompilerDirective OldCompilerDirective = PopCompilerDirective(Directive);
				switch (OldCompilerDirective)
				{
					case UhtCompilerDirective.ZeroBlock:
						PushCompilerDirective(UhtCompilerDirective.OneBlock);
						break;
					case UhtCompilerDirective.OneBlock:
						PushCompilerDirective(UhtCompilerDirective.ZeroBlock);
						break;
					case UhtCompilerDirective.NotCPPBlock:
						PushCompilerDirective(UhtCompilerDirective.CPPBlock);
						break;
					case UhtCompilerDirective.CPPBlock:
						PushCompilerDirective(UhtCompilerDirective.NotCPPBlock);
						break;
					case UhtCompilerDirective.WithHotReload:
						throw new UhtException(this.TokenReader, Directive.InputLine, "Can not use WITH_HOT_RELOAD with an #else clause");
					default:
						PushCompilerDirective(OldCompilerDirective);
						break;
				}
			}
			else if (Directive.IsValue("endif"))
			{
				PopCompilerDirective(Directive);
			}
			else if (Directive.IsValue("define"))
			{
			}
			else if (Directive.IsValue("undef"))
			{
			}
			else
			{
				if (bIsBeingIncluded)
				{
					throw new UhtException(this.TokenReader, Directive.InputLine, $"Unrecognized compiler directive {Directive.Value}");
				}
			}
			return bCheckClearComments;
		}

		private UhtCompilerDirective ParserConditional(UhtTokenBufferReader LineTokenReader)
		{
			// Get any possible ! and the identifier
			UhtToken Define = LineTokenReader.GetToken();
			bool bNotPresent = Define.IsSymbol('!');
			if (bNotPresent)
			{
				Define = LineTokenReader.GetToken();
			}

			//COMPATIBILITY-TODO
			// UModel.h contains a compound #if where the leading one is !CPP.
			// Checking for this being the only token causes that to fail
#if COMPATIBILITY_DISABLE
			// Make sure there is nothing left
			UhtToken End = LineTokenReader.GetToken();
			if (!End.TokenType.IsEndType())
			{
				return UhtCompilerDirective.Unrecognized;
			}
#endif

			switch (Define.TokenType)
			{
				case UhtTokenType.DecimalConst:
					if (Define.IsValue("0"))
					{
						return UhtCompilerDirective.ZeroBlock;
					}
					else if (Define.IsValue("1"))
					{
						return UhtCompilerDirective.OneBlock;
					}
					break;

				case UhtTokenType.Identifier:
					if (Define.IsValue("WITH_EDITORONLY_DATA"))
					{
						return bNotPresent ? UhtCompilerDirective.Unrecognized : UhtCompilerDirective.WithEditorOnlyData;
					}
					else if (Define.IsValue("WITH_EDITOR"))
					{
						return bNotPresent ? UhtCompilerDirective.Unrecognized : UhtCompilerDirective.WithEditor;
					}
					else if (Define.IsValue("WITH_HOT_RELOAD"))
					{
						return UhtCompilerDirective.WithHotReload;
					}
					else if (Define.IsValue("CPP"))
					{
						return bNotPresent ? UhtCompilerDirective.NotCPPBlock : UhtCompilerDirective.CPPBlock;
					}
					break;

				case UhtTokenType.EndOfFile:
				case UhtTokenType.EndOfDefault:
				case UhtTokenType.EndOfType:
				case UhtTokenType.EndOfDeclaration:
					throw new UhtException(this.TokenReader, Define.InputLine, "#if with no expression");
			}

			return UhtCompilerDirective.Unrecognized;
		}

		/// <summary>
		/// Add a new compiler directive to the stack
		/// </summary>
		/// <param name="CompilerDirective">Directive to be added</param>
		private void PushCompilerDirective(UhtCompilerDirective CompilerDirective)
		{
			CompilerDirective New = new CompilerDirective();
			New.Element = CompilerDirective;
			New.Composite = GetCurrentCompositeCompilerDirective() | CompilerDirective;
			this.CompilerDirectives.Add(New);
		}

		/// <summary>
		/// Remove the top level compiler directive from the stack
		/// </summary>
		private UhtCompilerDirective PopCompilerDirective(UhtToken Token)
		{
			if (this.CompilerDirectives.Count == 0)
			{
				throw new UhtException(this.TokenReader, Token.InputLine, $"Unmatched '#{Token.Value}'");
			}
			UhtCompilerDirective CompilerDirective = this.CompilerDirectives[this.CompilerDirectives.Count - 1].Element;
			this.CompilerDirectives.RemoveAt(this.CompilerDirectives.Count - 1);
			return CompilerDirective;
		}

		private bool IncludeCurrentCompilerDirective()
		{
			if (this.CompilerDirectives.Count == 0)
			{
				return true;
			}
			return (GetCurrentCompositeCompilerDirective() & (UhtCompilerDirective.NotCPPBlock | UhtCompilerDirective.OneBlock | UhtCompilerDirective.WithEditor | UhtCompilerDirective.WithEditorOnlyData | UhtCompilerDirective.WithHotReload)) != 0;
		}

		/// <summary>
		/// The old UHT would preprocess the file and eliminate any #if blocks that were not required for 
		/// any contextual information.  This results in comments before the #if block being considered 
		/// for the next definition.  This routine classifies each #if block type into if comments should
		/// be purged after the directive.
		/// </summary>
		/// <returns></returns>
		/// <exception cref="UhtIceException"></exception>
		private bool ClearCommentsCompilerDirective()
		{
			if (this.CompilerDirectives.Count == 0)
			{
				return true;
			}
			UhtCompilerDirective CompilerDirective = this.CompilerDirectives[this.CompilerDirectives.Count - 1].Element;
			switch (CompilerDirective)
			{
				case UhtCompilerDirective.CPPBlock:
				case UhtCompilerDirective.NotCPPBlock:
				case UhtCompilerDirective.ZeroBlock:
				case UhtCompilerDirective.OneBlock:
				case UhtCompilerDirective.Unrecognized:
					return false;

				case UhtCompilerDirective.WithEditor:
				case UhtCompilerDirective.WithEditorOnlyData:
				case UhtCompilerDirective.WithHotReload:
					return true;

				default:
					throw new UhtIceException("Unknown compiler directive flag");
			}
		}

		private bool SupportsElif(UhtCompilerDirective CompilerDirective)
		{
			return
				CompilerDirective == UhtCompilerDirective.WithEditor ||
				CompilerDirective == UhtCompilerDirective.WithEditorOnlyData ||
				CompilerDirective == UhtCompilerDirective.WithHotReload;
		}

		private string GetCompilerDirectiveText(UhtCompilerDirective CompilerDirective)
		{
			switch (CompilerDirective)
			{
				case UhtCompilerDirective.CPPBlock: return "CPP";
				case UhtCompilerDirective.NotCPPBlock: return "!CPP";
				case UhtCompilerDirective.ZeroBlock: return "0";
				case UhtCompilerDirective.OneBlock: return "1";
				case UhtCompilerDirective.WithHotReload: return "WITH_HOT_RELOAD";
				case UhtCompilerDirective.WithEditor: return "WITH_EDITOR";
				case UhtCompilerDirective.WithEditorOnlyData: return "WITH_EDITOR_DATA";
				default: return "<unrecognized>";
			}
		}

		private void SkipVirtualAndAPI(IUhtTokenReader ReplayReader)
		{
			while (true)
			{
				UhtToken PeekToken = ReplayReader.PeekToken();
				if (!PeekToken.IsValue("virtual") && !PeekToken.Value.Span.EndsWith("_API"))
				{
					break;
				}
				ReplayReader.ConsumeToken();
			}
		}

		private bool CheckForConstructor(UhtClass Class, UhtDeclaration Declaration)
		{
			IUhtTokenReader ReplayReader = UhtTokenReplayReader.GetThreadInstance(this.HeaderFile, this.HeaderFile.Data.Memory, new ReadOnlyMemory<UhtToken>(Declaration.Tokens), UhtTokenType.EndOfDeclaration);

			// Allow explicit constructors
			bool bFoundExplicit = ReplayReader.TryOptional("explicit");

			bool bSkippedAPIToken = false;
			if (bSkippedAPIToken = ReplayReader.PeekToken().Value.Span.EndsWith("_API"))
			{
				ReplayReader.ConsumeToken();
				if (!bFoundExplicit)
				{
					bFoundExplicit = ReplayReader.TryOptional("explicit");
				}
			}

			if (!ReplayReader.TryOptional(Class.SourceName) ||
				!ReplayReader.TryOptional('('))
			{
				return false;
			}

			bool bOICtor = false;
			bool bVTCtor = false;

			if (!Class.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.HasDefaultConstructor) && ReplayReader.TryOptional(')'))
			{
				Class.ClassExportFlags |= UhtClassExportFlags.HasDefaultConstructor;
			}
			else if (!Class.ClassExportFlags.HasAllFlags(UhtClassExportFlags.HasObjectInitializerConstructor | UhtClassExportFlags.HasCustomVTableHelperConstructor))
			{
				bool bIsConst = false;
				bool bIsRef = false;
				int ParenthesesNestingLevel = 1;

				while (ParenthesesNestingLevel != 0)
				{
					UhtToken Token = ReplayReader.GetToken();
					if (!Token)
					{
						break;
					}

					// Template instantiation or additional parameter excludes ObjectInitializer constructor.
					if (Token.IsValue(',') || Token.IsValue('<'))
					{
						bOICtor = false;
						bVTCtor = false;
						break;
					}

					if (Token.IsValue('('))
					{
						ParenthesesNestingLevel++;
						continue;
					}

					if (Token.IsValue(')'))
					{
						ParenthesesNestingLevel--;
						continue;
					}

					if (Token.IsValue("const"))
					{
						bIsConst = true;
						continue;
					}

					if (Token.IsValue('&'))
					{
						bIsRef = true;
						continue;
					}

					// FPostConstructInitializeProperties is deprecated, but left here, so it won't break legacy code.
					if (Token.IsValue("FObjectInitializer") || Token.IsValue("FPostConstructInitializeProperties"))
					{
						bOICtor = true;
					}

					if (Token.IsValue("FVTableHelper"))
					{
						bVTCtor = true;
					}
				}

				// Parse until finish.
				if (ParenthesesNestingLevel != 0)
				{
					ReplayReader.SkipBrackets('(', ')', ParenthesesNestingLevel);
				}

				if (bOICtor && bIsRef && bIsConst)
				{
					Class.ClassExportFlags |= UhtClassExportFlags.HasObjectInitializerConstructor;
					Class.MetaData.Add(UhtNames.ObjectInitializerConstructorDeclared, "");
				}
				if (bVTCtor && bIsRef)
				{
					Class.ClassExportFlags |= UhtClassExportFlags.HasCustomVTableHelperConstructor;
				}
			}

			if (!bVTCtor)
			{
				Class.ClassExportFlags |= UhtClassExportFlags.HasConstructor;
			}

			return false;
		}

		bool CheckForSerialize(UhtClass Class, UhtDeclaration Declaration)
		{
			IUhtTokenReader ReplayReader = UhtTokenReplayReader.GetThreadInstance(this.HeaderFile, this.HeaderFile.Data.Memory, new ReadOnlyMemory<UhtToken>(Declaration.Tokens), UhtTokenType.EndOfDeclaration);

			SkipVirtualAndAPI(ReplayReader);

			if (!ReplayReader.TryOptional("void") ||
				!ReplayReader.TryOptional("Serialize") ||
				!ReplayReader.TryOptional('('))
			{
				return false;
			}

			UhtToken Token = ReplayReader.GetToken();

			UhtSerializerArchiveType ArchiveType = UhtSerializerArchiveType.None;
			if (Token.IsValue("FArchive"))
			{
				if (ReplayReader.TryOptional('&'))
				{
					// Allow the declaration to not define a name for the archive parameter
					if (!ReplayReader.PeekToken().IsValue(')'))
					{
						ReplayReader.SkipOne();
					}
					if (ReplayReader.TryOptional(')'))
					{
						ArchiveType = UhtSerializerArchiveType.Archive;
					}
				}
			}
			else if (Token.IsValue("FStructuredArchive"))
			{
				if (ReplayReader.TryOptional("::") &&
					ReplayReader.TryOptional("FRecord"))
				{
					// Allow the declaration to not define a name for the archive parameter
					if (!ReplayReader.PeekToken().IsValue(')'))
					{
						ReplayReader.SkipOne();
					}
					if (ReplayReader.TryOptional(')'))
					{
						ArchiveType = UhtSerializerArchiveType.StructuredArchiveRecord;
					}
				}
			}
			else if (Token.IsValue("FStructuredArchiveRecord"))
			{
				// Allow the declaration to not define a name for the archive parameter
				if (!ReplayReader.PeekToken().IsValue(')'))
				{
					ReplayReader.SkipOne();
				}
				if (ReplayReader.TryOptional(')'))
				{
					ArchiveType = UhtSerializerArchiveType.StructuredArchiveRecord;
				}
			}

			if (ArchiveType != UhtSerializerArchiveType.None)
			{
				// Found what we want!
				if (Declaration.CompilerDirectives == UhtCompilerDirective.None || Declaration.CompilerDirectives == UhtCompilerDirective.WithEditorOnlyData)
				{
					Class.SerializerArchiveType |= ArchiveType;
					Class.EnclosingDefine = Declaration.CompilerDirectives == UhtCompilerDirective.None ? "" : "WITH_EDITORONLY_DATA";
				}
				else
				{
					Class.LogError("Serialize functions must not be inside preprocessor blocks, except for WITH_EDITORONLY_DATA");
				}
				return true;
			}

			return false;
		}
		#endregion
	}
}
