// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Text;

namespace EpicGames.UHT.Parsers
{
	public class UhtParsingScope : IDisposable
	{
		public readonly UhtHeaderFileParser HeaderParser;
		public readonly IUhtTokenReader TokenReader;
		public readonly UhtParsingScope? ParentScope;
		public readonly UhtType ScopeType;
		public readonly UhtKeywordTable ScopeKeywordTable;
		public UhtAccessSpecifier AccessSpecifier = UhtAccessSpecifier.Public;

		/// <summary>
		/// Return the current class scope being compiled
		/// </summary>
		public UhtParsingScope CurrentClassScope
		{
			get
			{
				UhtParsingScope? CurrentScope = this;
				while (CurrentScope != null)
				{
					if (CurrentScope.ScopeType is UhtClass Class)
					{
						return CurrentScope;
					}
					CurrentScope = CurrentScope.ParentScope;
				}
				throw new UhtIceException("Attempt to fetch the current class when a class isn't currently being parsed");
			}
		}

		/// <summary>
		/// Return the current class being compiled
		/// </summary>
		public UhtClass CurrentClass => (UhtClass)this.CurrentClassScope.ScopeType;

		public UhtParsingScope(UhtHeaderFileParser HeaderParser, UhtType ScopeType, UhtKeywordTable KeywordTable)
		{
			this.HeaderParser = HeaderParser;
			this.TokenReader = HeaderParser.TokenReader;
			this.ParentScope = null;
			this.ScopeType = ScopeType;
			this.ScopeKeywordTable = KeywordTable;
			this.HeaderParser.PushScope(this);
		}

		public UhtParsingScope(UhtParsingScope ParentScope, UhtType ScopeType, UhtKeywordTable KeywordTable, UhtAccessSpecifier AccessSpecifier)
		{
			this.HeaderParser = ParentScope.HeaderParser;
			this.TokenReader = ParentScope.TokenReader;
			this.ParentScope = ParentScope;
			this.ScopeType = ScopeType;
			this.ScopeKeywordTable = KeywordTable;
			this.AccessSpecifier = AccessSpecifier;
			this.HeaderParser.PushScope(this);
		}

		public void Dispose()
		{
			this.HeaderParser.PopScope(this);
		}

		/// <summary>
		/// Add the module's relative path to the type's meta data
		/// </summary>
		public void AddModuleRelativePathToMetaData()
		{
			AddModuleRelativePathToMetaData(this.ScopeType.MetaData, this.ScopeType.HeaderFile);
		}

		/// <summary>
		/// Add the module's relative path to the meta data
		/// </summary>
		/// <param name="MetaData">The meta data to add the information to</param>
		/// <param name="HeaderFile">The header file currently being parsed</param>
		public static void AddModuleRelativePathToMetaData(UhtMetaData MetaData, UhtHeaderFile HeaderFile)
		{
			MetaData.Add(UhtNames.ModuleRelativePath, HeaderFile.ModuleRelativeFilePath);
		}

		/// <summary>
		/// Format the current token reader comments and add it as meta data
		/// </summary>
		/// <param name="MetaNameIndex">Index for the meta data key.  This is used for enum values</param>
		public void AddFormattedCommentsAsTooltipMetaData(int MetaNameIndex = UhtMetaData.INDEX_NONE)
		{
			AddFormattedCommentsAsTooltipMetaData(this.ScopeType, MetaNameIndex);
		}

		/// <summary>
		/// Format the current token reader comments and add it as meta data
		/// </summary>
		/// <param name="Type">The type to add the meta data to</param>
		/// <param name="MetaNameIndex">Index for the meta data key.  This is used for enum values</param>
		public void AddFormattedCommentsAsTooltipMetaData(UhtType Type, int MetaNameIndex = UhtMetaData.INDEX_NONE)
		{

			// Don't add a tooltip if one already exists.
			if (Type.MetaData.ContainsKey(UhtNames.ToolTip, MetaNameIndex))
			{
				return;
			}

			// Fetch the comments
			ReadOnlySpan<StringView> Comments = this.TokenReader.Comments;

			// If we don't have any comments, just return
			string MergedString = String.Empty;
			if (Comments.Length == 0)
			{
				return;
			}

			// Set the comment as just a simple concatenation of all the strings
			if (Comments.Length == 1)
			{
				MergedString = Comments[0].ToString();
				Type.MetaData.Add(UhtNames.Comment, MetaNameIndex, Comments[0].ToString());
			}
			else
			{
				using (BorrowStringBuilder Borrower = new BorrowStringBuilder(StringBuilderCache.Small))
				{
					StringBuilder Builder = Borrower.StringBuilder;
					foreach (StringView Comment in Comments)
					{
						Builder.Append(Comment);
					}
					MergedString = Builder.ToString();
					Type.MetaData.Add(UhtNames.Comment, MetaNameIndex, MergedString);
				}
			}

			// Format the tooltip and set the metadata
			StringView ToolTip = FormatCommentForToolTip(MergedString);
			if (ToolTip.Span.Length > 0)
			{
				Type.MetaData.Add(UhtNames.ToolTip, MetaNameIndex, ToolTip.ToString());

				//COMPATIBILITY-TODO - Old UHT would only clear the comments if there was some form of a tooltip
				this.TokenReader.ClearComments();
			}

			//COMPATIBILITY-TODO
			// Clear the comments since they have been consumed
			//this.TokenReader.ClearComments();
		}

		// We consider any alpha/digit or code point > 0xFF as a valid comment char
		/// <summary>
		/// Given a list of comments, check to see if any have alpha, numeric, or unicode code points with a value larger than 0xFF.
		/// </summary>
		/// <param name="Comments">Comments to search</param>
		/// <returns>True is a character in question was found</returns>
		private static bool HasValidCommentChar(ReadOnlySpan<StringView> Comments)
		{
			foreach (StringView Comment in Comments)
			{
				if (HasValidCommentChar(Comment.Span))
				{
					return true;
				}
			}
			return false;
		}

		// We consider any alpha/digit or code point > 0xFF as a valid comment char
		/// <summary>
		/// Given a list of comments, check to see if any have alpha, numeric, or unicode code points with a value larger than 0xFF.
		/// </summary>
		/// <param name="Comments">Comments to search</param>
		/// <returns>True is a character in question was found</returns>
		private static bool HasValidCommentChar(ReadOnlySpan<char> Comments)
		{
			foreach (char C in Comments)
			{
				if (UhtFCString.IsAlnum(C) || C > 0xFF)
				{
					return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Convert the given list of comments to a tooltip.  Each string view is a comment where the // style comments also includes the trailing \r\n.
		/// 
		/// The following style comments are supported:
		/// 
		/// /* */ - C Style
		/// /** */ - C Style JavaDocs
		/// /*~ */ - C Style but ignore
		/// //\r\n - C++ Style
		/// ///\r\n - C++ Style JavaDocs
		/// //~\r\n - C++ Style bug ignore
		///
		/// As per TokenReader, there will only be one C style comment ever present, and it will be the first one.  When a C style comment is parsed, any prior comments
		/// are cleared.  However, if a C++ style comment follows a C style comment (regardless of any intermediate blank lines), then both blocks of comments will be present.
		/// If any blank lines are encountered between blocks of C++ style comments, then any prior comments are cleared.
		/// </summary>
		/// <param name="Comments">Comments to be parsed</param>
		/// <returns>The generated tooltip</returns>
		private static StringView FormatCommentForToolTip(string Comments)
		{
			if (!HasValidCommentChar(Comments))
			{
				return new StringView();
			}

			string Result = Comments;

			// Sweep out comments marked to be ignored.
			{
				int CommentStart, CommentEnd;

				// Block comments go first
				while ((CommentStart = Result.IndexOf("/*~")) != -1)
				{
					CommentEnd = Result.IndexOf("*/", CommentStart);
					if (CommentEnd != -1)
					{
						Result = Result.Remove(CommentStart, CommentEnd - CommentStart + 2);
					}
					else
					{
						// This looks like an error - an unclosed block comment.
						break;
					}
				}

				// Leftover line comments go next
				while ((CommentStart = Result.IndexOf("//~")) != -1)
				{
					CommentEnd = Result.IndexOf("\n", CommentStart);
					if (CommentEnd != -1)
					{
						Result = Result.Remove(CommentStart, CommentEnd - CommentStart + 1);
					}
					else
					{
						Result = Result.Remove(CommentStart);
						break;
					}
				}
			}

			// Check for known commenting styles.
			bool bJavaDocStyle = Result.Contains("/**");
			bool bCStyle = Result.Contains("/*");
			bool bCPPStyle = Result.StartsWith("//");

			if (bJavaDocStyle || bCStyle)
			{
				// Remove beginning and end markers.
				if (bJavaDocStyle)
				{
					Result = Result.Replace("/**", "");
				}
				if (bCStyle)
				{
					Result = Result.Replace("/*", "");
				}
				Result = Result.Replace("*/", "");
			}

			if (bCPPStyle)
			{
				// Remove c++-style comment markers.  Also handle javadoc-style comments 
				Result = Result.Replace("///", "");
				Result = Result.Replace("//", "");

				// Parser strips cpptext and replaces it with "// (cpptext)" -- prevent
				// this from being treated as a comment on variables declared below the
				// cpptext section
				Result = Result.Replace("(cpptext)", "");
			}

			// Get rid of carriage return or tab characters, which mess up tooltips.
			Result = Result.Replace("\r", "");

			//wx widgets has a hard coded tab size of 8
			{
				int SpacesPerTab = 8;
				Result = UhtFCString.TabsToSpaces(Result, SpacesPerTab, true);
			}

			// get rid of uniform leading whitespace and all trailing whitespace, on each line
			string[] Lines = Result.Split('\n');

			for (int Index = 0; Index < Lines.Length; ++Index)
			{
				// Remove trailing whitespace
				string Line = Lines[Index].TrimEnd();

				// Remove leading "*" and "* " in javadoc comments.
				if (bJavaDocStyle)
				{
					// Find first non-whitespace character
					int Pos = 0;
					while (Pos < Line.Length && UhtFCString.IsWhitespace(Line[Pos]))
					{
						++Pos;
					}

					// Is it a *?
					if (Pos < Line.Length && Line[Pos] == '*')
					{
						// Eat next space as well
						if (Pos + 1 < Line.Length && UhtFCString.IsWhitespace(Line[Pos + 1]))
						{
							++Pos;
						}

						Line = Line.Substring(Pos + 1);
					}
				}
				Lines[Index] = Line;
			}

			Func<string, int, char, bool> IsAllSameChar = (string Line, int StartIndex, char TestChar) =>
			{
				for (int Index = StartIndex, End = Line.Length; Index < End; ++Index)
				{
					if (Line[Index] != TestChar)
					{
						return false;
					}
				}
				return true;
			};

			Func<string, bool> IsWhitespaceOrLineSeparator = (string Line) =>
			{
				// Skip any leading spaces
				int Index = 0;
				int EndPos = Line.Length;
				for (; Index < EndPos && UhtFCString.IsWhitespace(Line[Index]); ++Index) { }
				if (Index == EndPos)
				{
					return true;
				}

				// Check for the same character
				return IsAllSameChar(Line, Index, '-') || IsAllSameChar(Line, Index, '=') || IsAllSameChar(Line, Index, '*');
			};

			// Find first meaningful line
			int FirstIndex = 0;
			for (; FirstIndex < Lines.Length && IsWhitespaceOrLineSeparator(Lines[FirstIndex]); ++FirstIndex)
			{ }

			int LastIndex = Lines.Length;
			for (; LastIndex > FirstIndex && IsWhitespaceOrLineSeparator(Lines[LastIndex - 1]); --LastIndex)
			{ }

			Result = String.Empty;
			if (FirstIndex != LastIndex)
			{
				string FirstLine = Lines[FirstIndex];

				// Figure out how much whitespace is on the first line
				int MaxNumWhitespaceToRemove = 0;
				for (; MaxNumWhitespaceToRemove < FirstLine.Length; MaxNumWhitespaceToRemove++)
				{
					if (/*!UhtFCString.IsLinebreak(FirstLine[MaxNumWhitespaceToRemove]) && */!UhtFCString.IsWhitespace(FirstLine[MaxNumWhitespaceToRemove]))
					{
						break;
					}
				}

				for (int Index = FirstIndex; Index != LastIndex; ++Index)
				{
					string Line = Lines[Index];

					int TemporaryMaxWhitespace = MaxNumWhitespaceToRemove;

					// Allow eating an extra tab on subsequent lines if it's present
					if ((Index > FirstIndex) && (Line.Length > 0) && (Line[0] == '\t'))
					{
						TemporaryMaxWhitespace++;
					}

					// Advance past whitespace
					int Pos = 0;
					while (Pos < TemporaryMaxWhitespace && Pos < Line.Length && UhtFCString.IsWhitespace(Line[Pos]))
					{
						++Pos;
					}

					if (Pos > 0)
					{
						Line = Line.Substring(Pos);
					}

					if (Index > FirstIndex)
					{
						Result += "\n";
					}

					if (Line.Length > 0 && !IsAllSameChar(Line, 0, '='))
					{
						Result += Line;
					}
				}
			}

			//@TODO: UCREMOVAL: Really want to trim an arbitrary number of newlines above and below, but keep multiple newlines internally
			// Make sure it doesn't start with a newline
			if (Result.Length != 0 && UhtFCString.IsLinebreak(Result[0]))
			{
				Result = Result.Substring(1);
			}

			// Make sure it doesn't end with a dead newline
			if (Result.Length != 0 && UhtFCString.IsLinebreak(Result[Result.Length - 1]))
			{
				Result = Result.Substring(0, Result.Length - 1);
			}

			// Done.
			return Result;
		}
	}

	public struct UhtTokenRecorder : IDisposable
	{
		private readonly UhtCompilerDirective CompilerDirective;
		private readonly UhtParsingScope Scope;
		private bool bFlushed;

		public UhtTokenRecorder(UhtParsingScope Scope, ref UhtToken InitialToken)
		{
			this.Scope = Scope;
			this.CompilerDirective = this.Scope.HeaderParser.GetCurrentCompositeCompilerDirective();
			this.bFlushed = false;
			if (this.Scope.ScopeType is UhtClass)
			{

				this.Scope.TokenReader.EnableRecording();
				this.Scope.TokenReader.RecordToken(ref InitialToken);
			}
		}

		public UhtTokenRecorder(UhtParsingScope Scope)
		{
			this.Scope = Scope;
			this.CompilerDirective = this.Scope.HeaderParser.GetCurrentCompositeCompilerDirective();
			this.bFlushed = false;
			if (this.Scope.ScopeType is UhtClass)
			{
				this.Scope.TokenReader.EnableRecording();
			}
		}

		public void Dispose()
		{
			Stop();
		}

		public bool Stop()
		{
			if (!bFlushed)
			{
				bFlushed = true;
				if (this.Scope.ScopeType is UhtClass Class)
				{
					Class.AddDeclaration(this.CompilerDirective, this.Scope.TokenReader.RecordedTokens);
					this.Scope.TokenReader.DisableRecording();
					return true;
				}
			}
			return false;
		}
	}
}
