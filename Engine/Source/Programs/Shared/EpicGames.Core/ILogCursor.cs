// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics.CodeAnalysis;
using System.Text.RegularExpressions;

namespace EpicGames.Core
{
	/// <summary>
	/// Allows querying the input text from the current cursor position
	/// </summary>
	public interface ILogCursor
	{
		/// <summary>
		/// Text for the current line
		/// </summary>
		string? CurrentLine
		{
			get;
		}

		/// <summary>
		/// The current line number
		/// </summary>
		int CurrentLineNumber
		{
			get;
		}

		/// <summary>
		/// Index to find the string at the given offset
		/// </summary>
		/// <param name="offset"></param>
		/// <returns></returns>
		string? this[int offset]
		{
			get;
		}
	}

	/// <summary>
	/// Extension methods for log cursors
	/// </summary>
	public static partial class LogCursorExtensions
	{
		/// <summary>
		/// Implementation of ILogCursor which positions the cursor at a fixed offset from the inner cursor
		/// </summary>
		class RebasedLogCursor : ILogCursor
		{
			readonly ILogCursor _inner;
			readonly int _baseLineNumber;

			public RebasedLogCursor(ILogCursor inner, int baseLineNumber)
			{
				_inner = inner;
				_baseLineNumber = baseLineNumber;
			}

			public string? this[int index] => _inner[(_baseLineNumber + index) - _inner.CurrentLineNumber];
			public string? CurrentLine => _inner[_baseLineNumber - _inner.CurrentLineNumber];
			public int CurrentLineNumber => _baseLineNumber;
		}

		class HangingIndentLogCursor : ILogCursor
		{
			readonly ILogCursor _inner;
			readonly int _indent;

			public HangingIndentLogCursor(ILogCursor inner)
			{
				_inner = inner;
				_indent = 0;

				string? line = inner.CurrentLine;
				if (line != null)
				{
					while (_indent < line.Length && Char.IsWhiteSpace(line[_indent]))
					{
						_indent++;
					}
				}
			}

			public string? this[int index]
			{
				get
				{
					if (index <= 0)
					{
						return _inner[index];
					}

					string? line = _inner[index];
					if (line != null && HasPrefix(line, _inner.CurrentLine!, _indent))
					{
						return line;
					}

					return null;
				}
			}

			static bool HasPrefix(string line, string prefix, int prefixLength)
			{
				if (line.Length > prefixLength && String.CompareOrdinal(line, 0, prefix, 0, prefixLength) == 0 && Char.IsWhiteSpace(line[prefixLength]))
				{
					return true;
				}
				if(IsWhitespace(line))
				{
					return true;
				}
				return false;
			}

			static bool IsWhitespace(string line)
			{
				for (int idx = 0; idx < line.Length; idx++)
				{
					if (!Char.IsWhiteSpace(line[idx]))
					{
						return false;
					}
				}
				return true;
			}

			public string? CurrentLine => _inner.CurrentLine;
			public int CurrentLineNumber => _inner.CurrentLineNumber;
		}

		/// <summary>
		/// Creates a new log cursor based at an offset from the current line
		/// </summary>
		/// <param name="cursor">The current log cursor instance</param>
		/// <param name="offset">Line number offset from the current</param>
		/// <returns>New log cursor instance</returns>
		public static ILogCursor Rebase(this ILogCursor cursor, int offset)
		{
			return new RebasedLogCursor(cursor, cursor.CurrentLineNumber + offset);
		}

		/// <summary>
		/// Creates a new log cursor which enumerates lines with a hanging indent
		/// </summary>
		/// <param name="cursor">The current log cursor instance</param>
		/// <returns>New log cursor instance</returns>
		public static ILogCursor Hanging(this ILogCursor cursor)
		{
			return new HangingIndentLogCursor(cursor);
		}

		/// <summary>
		/// Attempts to get a line at the given offset
		/// </summary>
		/// <param name="cursor">The log cursor instance</param>
		/// <param name="offset">Offset of the line to retrieve</param>
		/// <param name="nextLine">On success, receives the matched line</param>
		/// <returns>True if the line was retrieved</returns>
		public static bool TryGetLine(this ILogCursor cursor, int offset, [NotNullWhen(true)] out string? nextLine)
		{
			nextLine = cursor[offset];
			return nextLine != null;
		}

		/// <summary>
		/// Tests whether the current line contains the given text
		/// </summary>
		/// <param name="cursor">The log cursor instance</param>
		/// <param name="text">The text to look for</param>
		/// <returns>True if the line contains the given string</returns>
		public static bool Contains(this ILogCursor cursor, string text) => Contains(cursor, 0, text);

		/// <summary>
		/// Tests whether the current line contains the given text
		/// </summary>
		/// <param name="cursor">The log cursor instance</param>
		/// <param name="offset">Offset of the line to match</param>
		/// <param name="text">The text to look for</param>
		/// <returns>True if the line contains the given string</returns>
		public static bool Contains(this ILogCursor cursor, int offset, string text)
		{
			string? line;
			return cursor.TryGetLine(offset, out line) && line.Contains(text, StringComparison.Ordinal);
		}

		/// <summary>
		/// Tests whether the current line starts with the given text
		/// </summary>
		/// <param name="cursor">The log cursor instance</param>
		/// <param name="text">The text to look for</param>
		/// <returns>True if the line starts with the given string</returns>
		public static bool StartsWith(this ILogCursor cursor, string text) => StartsWith(cursor, 0, text);

		/// <summary>
		/// Tests whether the current line starts with the given text
		/// </summary>
		/// <param name="cursor">The log cursor instance</param>
		/// <param name="offset">Offset of the line to match</param>
		/// <param name="text">The text to look for</param>
		/// <returns>True if the line starts with the given string</returns>
		public static bool StartsWith(this ILogCursor cursor, int offset, string text)
		{
			string? line;
			return cursor.TryGetLine(offset, out line) && line.StartsWith(text, StringComparison.Ordinal);
		}

		/// <summary>
		/// Determines if the current line matches the given regex
		/// </summary>
		/// <param name="cursor">The log cursor instance</param>
		/// <param name="pattern">The regex pattern to match</param>
		/// <returns>True if the current line matches the given patter</returns>
		public static bool IsMatch(this ILogCursor cursor, Regex pattern)
		{
			return IsMatch(cursor, 0, pattern);
		}

		/// <summary>
		/// Determines if the line at the given offset matches the given regex
		/// </summary>
		/// <param name="cursor">The log cursor instance</param>
		/// <param name="offset">Offset of the line to match</param>
		/// <param name="pattern">The regex pattern to match</param>
		/// <returns>True if the requested line matches the given patter</returns>
		public static bool IsMatch(this ILogCursor cursor, int offset, Regex pattern)
		{
			string? line;
			return cursor.TryGetLine(offset, out line) && pattern.IsMatch(line!);
		}

		/// <summary>
		/// Determines if the current line matches the given regex
		/// </summary>
		/// <param name="cursor">The log cursor instance</param>
		/// <param name="pattern">The regex pattern to match</param>
		/// <param name="outMatch">On success, receives the match result</param>
		/// <returns>True if the current line matches the given patter</returns>
		public static bool TryMatch(this ILogCursor cursor, Regex pattern, [NotNullWhen(true)] out Match? outMatch)
		{
			return TryMatch(cursor, 0, pattern, out outMatch);
		}

		/// <summary>
		/// Determines if the line at the given offset matches the given regex
		/// </summary>
		/// <param name="cursor">The log cursor instance</param>
		/// <param name="offset">The line offset to check</param>
		/// <param name="pattern">The regex pattern to match</param>
		/// <param name="outMatch">On success, receives the match result</param>
		/// <returns>True if the current line matches the given patter</returns>
		public static bool TryMatch(this ILogCursor cursor, int offset, Regex pattern, [NotNullWhen(true)] out Match? outMatch)
		{
			string? line;
			if (!cursor.TryGetLine(offset, out line))
			{
				outMatch = null;
				return false;
			}

			Match match = pattern.Match(line);
			if (!match.Success)
			{
				outMatch = null;
				return false;
			}

			outMatch = match;
			return true;
		}

		/// <summary>
		/// Matches lines forward from the given offset while the given pattern matches
		/// </summary>
		/// <param name="cursor">The log cursor instance</param>
		/// <param name="offset">Initial offset</param>
		/// <param name="pattern">Pattern to match</param>
		/// <returns>Offset of the last line that still matches the pattern (inclusive)</returns>
		public static int MatchForwards(this ILogCursor cursor, int offset, Regex pattern)
		{
			while (IsMatch(cursor, offset + 1, pattern))
			{
				offset++;
			}
			return offset;
		}

		/// <summary>
		/// Matches lines forwards from the given offset until the given pattern matches
		/// </summary>
		/// <param name="cursor">The log cursor</param>
		/// <param name="offset">Initial offset</param>
		/// <param name="pattern">Pattern to match</param>
		/// <returns>Offset of the line that matches the pattern (inclusive), or EOF is encountered</returns>
		public static int MatchForwardsUntil(this ILogCursor cursor, int offset, Regex pattern)
		{
			string? nextLine;
			for (int nextOffset = offset + 1; cursor.TryGetLine(nextOffset, out nextLine); nextOffset++)
			{
				if (pattern.IsMatch(nextLine))
				{
					return nextOffset;
				}
			}
			return offset;
		}
	}
}
