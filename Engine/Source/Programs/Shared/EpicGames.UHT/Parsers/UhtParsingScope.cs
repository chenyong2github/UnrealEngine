// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{

	/// <summary>
	/// Nested structure of scopes being parsed
	/// </summary>
	public class UhtParsingScope : IDisposable
	{

		/// <summary>
		/// Header file parser
		/// </summary>
		public UhtHeaderFileParser HeaderParser { get; }

		/// <summary>
		/// Token reader
		/// </summary>
		public IUhtTokenReader TokenReader { get; }

		/// <summary>
		/// Parent scope
		/// </summary>
		public UhtParsingScope? ParentScope { get; }

		/// <summary>
		/// Type being parsed
		/// </summary>
		public UhtType ScopeType { get; }

		/// <summary>
		/// Keyword table for the scope
		/// </summary>
		public UhtKeywordTable ScopeKeywordTable { get; }

		/// <summary>
		/// Current access specifier
		/// </summary>
		public UhtAccessSpecifier AccessSpecifier { get; set; } = UhtAccessSpecifier.Public;

		/// <summary>
		/// Current session
		/// </summary>
		public UhtSession Session => this.ScopeType.Session;

		/// <summary>
		/// Return the current class scope being compiled
		/// </summary>
		public UhtParsingScope CurrentClassScope
		{
			get
			{
				UhtParsingScope? currentScope = this;
				while (currentScope != null)
				{
					if (currentScope.ScopeType is UhtClass)
					{
						return currentScope;
					}
					currentScope = currentScope.ParentScope;
				}
				throw new UhtIceException("Attempt to fetch the current class when a class isn't currently being parsed");
			}
		}

		/// <summary>
		/// Return the current class being compiled
		/// </summary>
		public UhtClass CurrentClass => (UhtClass)this.CurrentClassScope.ScopeType;

		/// <summary>
		/// Construct a root/global scope
		/// </summary>
		/// <param name="headerParser">Header parser</param>
		/// <param name="scopeType">Type being parsed</param>
		/// <param name="keywordTable">Keyword table</param>
		public UhtParsingScope(UhtHeaderFileParser headerParser, UhtType scopeType, UhtKeywordTable keywordTable)
		{
			this.HeaderParser = headerParser;
			this.TokenReader = headerParser.TokenReader;
			this.ParentScope = null;
			this.ScopeType = scopeType;
			this.ScopeKeywordTable = keywordTable;
			this.HeaderParser.PushScope(this);
		}

		/// <summary>
		/// Construct a scope for a type
		/// </summary>
		/// <param name="parentScope">Parent scope</param>
		/// <param name="scopeType">Type being parsed</param>
		/// <param name="keywordTable">Keyword table</param>
		/// <param name="accessSpecifier">Current access specifier</param>
		public UhtParsingScope(UhtParsingScope parentScope, UhtType scopeType, UhtKeywordTable keywordTable, UhtAccessSpecifier accessSpecifier)
		{
			this.HeaderParser = parentScope.HeaderParser;
			this.TokenReader = parentScope.TokenReader;
			this.ParentScope = parentScope;
			this.ScopeType = scopeType;
			this.ScopeKeywordTable = keywordTable;
			this.AccessSpecifier = accessSpecifier;
			this.HeaderParser.PushScope(this);
		}

		/// <summary>
		/// Dispose the scope
		/// </summary>
		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		/// <summary>
		/// Virtual method for disposing the object
		/// </summary>
		/// <param name="disposing">If true, we are disposing</param>
		protected virtual void Dispose(bool disposing)
		{
			if (disposing)
			{
				this.HeaderParser.PopScope(this);
			}
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
		/// <param name="metaData">The meta data to add the information to</param>
		/// <param name="headerFile">The header file currently being parsed</param>
		public static void AddModuleRelativePathToMetaData(UhtMetaData metaData, UhtHeaderFile headerFile)
		{
			metaData.Add(UhtNames.ModuleRelativePath, headerFile.ModuleRelativeFilePath);
		}

		/// <summary>
		/// Format the current token reader comments and add it as meta data
		/// </summary>
		/// <param name="metaNameIndex">Index for the meta data key.  This is used for enum values</param>
		public void AddFormattedCommentsAsTooltipMetaData(int metaNameIndex = UhtMetaData.IndexNone)
		{
			AddFormattedCommentsAsTooltipMetaData(this.ScopeType, metaNameIndex);
		}

		/// <summary>
		/// Format the current token reader comments and add it as meta data
		/// </summary>
		/// <param name="type">The type to add the meta data to</param>
		/// <param name="metaNameIndex">Index for the meta data key.  This is used for enum values</param>
		public void AddFormattedCommentsAsTooltipMetaData(UhtType type, int metaNameIndex = UhtMetaData.IndexNone)
		{

			// Don't add a tooltip if one already exists.
			if (type.MetaData.ContainsKey(UhtNames.ToolTip, metaNameIndex))
			{
				return;
			}

			// Fetch the comments
			ReadOnlySpan<StringView> comments = this.TokenReader.Comments;

			// If we don't have any comments, just return
			string mergedString = String.Empty;
			if (comments.Length == 0)
			{
				return;
			}

			// Set the comment as just a simple concatenation of all the strings
			if (comments.Length == 1)
			{
				mergedString = comments[0].ToString();
				type.MetaData.Add(UhtNames.Comment, metaNameIndex, comments[0].ToString());
			}
			else
			{
				using BorrowStringBuilder borrower = new(StringBuilderCache.Small);
				StringBuilder builder = borrower.StringBuilder;
				foreach (StringView comment in comments)
				{
					builder.Append(comment);
				}
				mergedString = builder.ToString();
				type.MetaData.Add(UhtNames.Comment, metaNameIndex, mergedString);
			}

			// Format the tooltip and set the metadata
			StringView toolTip = FormatCommentForToolTip(mergedString);
			if (toolTip.Span.Length > 0)
			{
				type.MetaData.Add(UhtNames.ToolTip, metaNameIndex, toolTip.ToString());

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
		/// <param name="comments">Comments to search</param>
		/// <returns>True is a character in question was found</returns>
		private static bool HasValidCommentChar(ReadOnlySpan<char> comments)
		{
			foreach (char c in comments)
			{
				if (UhtFCString.IsAlnum(c) || c > 0xFF)
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
		/// <param name="comments">Comments to be parsed</param>
		/// <returns>The generated tooltip</returns>
		private static StringView FormatCommentForToolTip(string comments)
		{
			if (!HasValidCommentChar(comments))
			{
				return new StringView();
			}

			string result = comments;

			// Sweep out comments marked to be ignored.
			{
				int commentStart, commentEnd;

				// Block comments go first
				while ((commentStart = result.IndexOf("/*~", StringComparison.Ordinal)) != -1)
				{
					commentEnd = result.IndexOf("*/", commentStart, StringComparison.Ordinal);
					if (commentEnd != -1)
					{
						result = result.Remove(commentStart, commentEnd - commentStart + 2);
					}
					else
					{
						// This looks like an error - an unclosed block comment.
						break;
					}
				}

				// Leftover line comments go next
				while ((commentStart = result.IndexOf("//~", StringComparison.Ordinal)) != -1)
				{
					commentEnd = result.IndexOf("\n", commentStart, StringComparison.Ordinal);
					if (commentEnd != -1)
					{
						result = result.Remove(commentStart, commentEnd - commentStart + 1);
					}
					else
					{
						result = result.Remove(commentStart);
						break;
					}
				}
			}

			// Check for known commenting styles.
			bool javaDocStyle = result.Contains("/**", StringComparison.Ordinal);
			bool cStyle = result.Contains("/*", StringComparison.Ordinal);
			bool cppStyle = result.StartsWith("//", StringComparison.Ordinal);

			if (javaDocStyle || cStyle)
			{
				// Remove beginning and end markers.
				if (javaDocStyle)
				{
					result = result.Replace("/**", "", StringComparison.Ordinal);
				}
				if (cStyle)
				{
					result = result.Replace("/*", "", StringComparison.Ordinal);
				}
				result = result.Replace("*/", "", StringComparison.Ordinal);
			}

			if (cppStyle)
			{
				// Remove c++-style comment markers.  Also handle javadoc-style comments 
				result = result.Replace("///", "", StringComparison.Ordinal);
				result = result.Replace("//", "", StringComparison.Ordinal);

				// Parser strips cpptext and replaces it with "// (cpptext)" -- prevent
				// this from being treated as a comment on variables declared below the
				// cpptext section
				result = result.Replace("(cpptext)", "", StringComparison.Ordinal);
			}

			// Get rid of carriage return or tab characters, which mess up tooltips.
			result = result.Replace("\r", "", StringComparison.Ordinal);

			//wx widgets has a hard coded tab size of 8
			{
				const int SpacesPerTab = 8;
				result = UhtFCString.TabsToSpaces(result, SpacesPerTab, true);
			}

			// get rid of uniform leading whitespace and all trailing whitespace, on each line
			string[] lines = result.Split('\n');

			for (int index = 0; index < lines.Length; ++index)
			{
				// Remove trailing whitespace
				string line = lines[index].TrimEnd();

				// Remove leading "*" and "* " in javadoc comments.
				if (javaDocStyle)
				{
					// Find first non-whitespace character
					int pos = 0;
					while (pos < line.Length && UhtFCString.IsWhitespace(line[pos]))
					{
						++pos;
					}

					// Is it a *?
					if (pos < line.Length && line[pos] == '*')
					{
						// Eat next space as well
						if (pos + 1 < line.Length && UhtFCString.IsWhitespace(line[pos + 1]))
						{
							++pos;
						}

						line = line[(pos + 1)..];
					}
				}
				lines[index] = line;
			}

			static bool IsAllSameChar(string line, int startIndex, char testChar)
			{
				for (int index = startIndex, end = line.Length; index < end; ++index)
				{
					if (line[index] != testChar)
					{
						return false;
					}
				}
				return true;
			}

			static bool IsWhitespaceOrLineSeparator(string line)
			{
				// Skip any leading spaces
				int index = 0;
				int endPos = line.Length;
				for (; index < endPos && UhtFCString.IsWhitespace(line[index]); ++index)
				{
				}
				if (index == endPos)
				{
					return true;
				}

				// Check for the same character
				return IsAllSameChar(line, index, '-') || IsAllSameChar(line, index, '=') || IsAllSameChar(line, index, '*');
			}

			// Find first meaningful line
			int firstIndex = 0;
			for (; firstIndex < lines.Length && IsWhitespaceOrLineSeparator(lines[firstIndex]); ++firstIndex)
			{ }

			int lastIndex = lines.Length;
			for (; lastIndex > firstIndex && IsWhitespaceOrLineSeparator(lines[lastIndex - 1]); --lastIndex)
			{ }

			result = String.Empty;
			if (firstIndex != lastIndex)
			{
				string firstLine = lines[firstIndex];

				// Figure out how much whitespace is on the first line
				int maxNumWhitespaceToRemove = 0;
				for (; maxNumWhitespaceToRemove < firstLine.Length; maxNumWhitespaceToRemove++)
				{
					if (/*!UhtFCString.IsLinebreak(FirstLine[MaxNumWhitespaceToRemove]) && */!UhtFCString.IsWhitespace(firstLine[maxNumWhitespaceToRemove]))
					{
						break;
					}
				}

				for (int index = firstIndex; index != lastIndex; ++index)
				{
					string line = lines[index];

					int temporaryMaxWhitespace = maxNumWhitespaceToRemove;

					// Allow eating an extra tab on subsequent lines if it's present
					if ((index > firstIndex) && (line.Length > 0) && (line[0] == '\t'))
					{
						temporaryMaxWhitespace++;
					}

					// Advance past whitespace
					int pos = 0;
					while (pos < temporaryMaxWhitespace && pos < line.Length && UhtFCString.IsWhitespace(line[pos]))
					{
						++pos;
					}

					if (pos > 0)
					{
						line = line[pos..];
					}

					if (index > firstIndex)
					{
						result += "\n";
					}

					if (line.Length > 0 && !IsAllSameChar(line, 0, '='))
					{
						result += line;
					}
				}
			}

			//@TODO: UCREMOVAL: Really want to trim an arbitrary number of newlines above and below, but keep multiple newlines internally
			// Make sure it doesn't start with a newline
			if (result.Length != 0 && UhtFCString.IsLinebreak(result[0]))
			{
				result = result[1..];
			}

			// Make sure it doesn't end with a dead newline
			if (result.Length != 0 && UhtFCString.IsLinebreak(result[^1]))
			{
				result = result[0..^1];
			}

			// Done.
			return result;
		}
	}

	/// <summary>
	/// Token recorder
	/// </summary>
	public struct UhtTokenRecorder : IDisposable
	{
		private readonly UhtCompilerDirective _compilerDirective;
		private readonly UhtParsingScope _scope;
		private readonly UhtFunction? _function;
		private bool _flushed;

		/// <summary>
		/// Construct a new recorder
		/// </summary>
		/// <param name="scope">Scope being parsed</param>
		/// <param name="initialToken">Initial toke nto add to the recorder</param>
		public UhtTokenRecorder(UhtParsingScope scope, ref UhtToken initialToken)
		{
			this._scope = scope;
			this._compilerDirective = this._scope.HeaderParser.GetCurrentCompositeCompilerDirective();
			this._function = null;
			this._flushed = false;
			if (this._scope.ScopeType is UhtClass)
			{
				this._scope.TokenReader.EnableRecording();
				this._scope.TokenReader.RecordToken(ref initialToken);
			}
		}

		/// <summary>
		/// Create a new recorder
		/// </summary>
		/// <param name="scope">Scope being parsed</param>
		/// <param name="function">Function associated with the recorder</param>
		public UhtTokenRecorder(UhtParsingScope scope, UhtFunction function)
		{
			this._scope = scope;
			this._compilerDirective = this._scope.HeaderParser.GetCurrentCompositeCompilerDirective();
			this._function = function;
			this._flushed = false;
			if (this._scope.ScopeType is UhtClass)
			{
				this._scope.TokenReader.EnableRecording();
			}
		}

		/// <summary>
		/// Stop the recording
		/// </summary>
		public void Dispose()
		{
			Stop();
		}

		/// <summary>
		/// Stop the recording
		/// </summary>
		/// <returns>True if the recorded content was added to a class</returns>
		public bool Stop()
		{
			if (!_flushed)
			{
				_flushed = true;
				if (this._scope.ScopeType is UhtClass classObj)
				{
					classObj.AddDeclaration(this._compilerDirective, this._scope.TokenReader.RecordedTokens, this._function);
					this._scope.TokenReader.DisableRecording();
					return true;
				}
			}
			return false;
		}
	}
}
