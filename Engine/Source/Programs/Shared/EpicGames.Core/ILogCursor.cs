// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;
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
		/// <param name="Offset"></param>
		/// <returns></returns>
		string? this[int Offset]
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
			ILogCursor Inner { get; }
			int BaseLineNumber { get; }

			public RebasedLogCursor(ILogCursor Inner, int BaseLineNumber)
			{
				this.Inner = Inner;
				this.BaseLineNumber = BaseLineNumber;
			}

			public string? this[int Index] => Inner[(BaseLineNumber + Index) - Inner.CurrentLineNumber];
			public string? CurrentLine => Inner[BaseLineNumber - Inner.CurrentLineNumber];
			public int CurrentLineNumber => BaseLineNumber;
		}

		/// <summary>
		/// Creates a new log cursor based at an offset from the current line
		/// </summary>
		/// <param name="Cursor">The current log cursor instance</param>
		/// <param name="Offset">Line number offset from the current</param>
		/// <returns>New log cursor instance</returns>
		public static ILogCursor Rebase(this ILogCursor Cursor, int Offset)
		{
			return new RebasedLogCursor(Cursor, Cursor.CurrentLineNumber + Offset);
		}

		/// <summary>
		/// Attempts to get a line at the given offset
		/// </summary>
		/// <param name="Cursor">The log cursor instance</param>
		/// <param name="Offset">Offset of the line to retrieve</param>
		/// <param name="NextLine">On success, receives the matched line</param>
		/// <returns>True if the line was retrieved</returns>
		public static bool TryGetLine(this ILogCursor Cursor, int Offset, [NotNullWhen(true)] out string? NextLine)
		{
			NextLine = Cursor[Offset];
			return NextLine != null;
		}

		/// <summary>
		/// Determines if the current line matches the given regex
		/// </summary>
		/// <param name="Cursor">The log cursor instance</param>
		/// <param name="Pattern">The regex pattern to match</param>
		/// <returns>True if the current line matches the given patter</returns>
		public static bool IsMatch(this ILogCursor Cursor, string Pattern)
		{
			return IsMatch(Cursor, 0, Pattern);
		}

		/// <summary>
		/// Determines if the line at the given offset matches the given regex
		/// </summary>
		/// <param name="Cursor">The log cursor instance</param>
		/// <param name="Offset">Offset of the line to match</param>
		/// <param name="Pattern">The regex pattern to match</param>
		/// <returns>True if the requested line matches the given patter</returns>
		public static bool IsMatch(this ILogCursor Cursor, int Offset, string Pattern)
		{
			string? Line;
			return Cursor.TryGetLine(Offset, out Line) && Regex.IsMatch(Line!, Pattern);
		}

		/// <summary>
		/// Determines if the current line matches the given regex
		/// </summary>
		/// <param name="Cursor">The log cursor instance</param>
		/// <param name="Pattern">The regex pattern to match</param>
		/// <param name="OutMatch">On success, receives the match result</param>
		/// <returns>True if the current line matches the given patter</returns>
		public static bool TryMatch(this ILogCursor Cursor, string Pattern, [NotNullWhen(true)] out Match? OutMatch)
		{
			return TryMatch(Cursor, 0, Pattern, out OutMatch);
		}

		/// <summary>
		/// Determines if the line at the given offset matches the given regex
		/// </summary>
		/// <param name="Cursor">The log cursor instance</param>
		/// <param name="Offset">The line offset to check</param>
		/// <param name="Pattern">The regex pattern to match</param>
		/// <param name="OutMatch">On success, receives the match result</param>
		/// <returns>True if the current line matches the given patter</returns>
		public static bool TryMatch(this ILogCursor Cursor, int Offset, string Pattern, [NotNullWhen(true)] out Match? OutMatch)
		{
			string? Line;
			if (!Cursor.TryGetLine(Offset, out Line))
			{
				OutMatch = null;
				return false;
			}

			Match Match = Regex.Match(Line, Pattern);
			if (!Match.Success)
			{
				OutMatch = null;
				return false;
			}

			OutMatch = Match;
			return true;
		}

		/// <summary>
		/// Matches lines forward from the given offset while the given pattern matches
		/// </summary>
		/// <param name="Cursor">The log cursor instance</param>
		/// <param name="Offset">Initial offset</param>
		/// <param name="Pattern">Pattern to match</param>
		/// <returns>Offset of the last line that still matches the pattern (inclusive)</returns>
		public static int MatchForwards(this ILogCursor Cursor, int Offset, string Pattern)
		{
			while (IsMatch(Cursor, Offset + 1, Pattern))
			{
				Offset++;
			}
			return Offset;
		}

		/// <summary>
		/// Matches lines forwards from the given offset until the given pattern matches
		/// </summary>
		/// <param name="Cursor">The log cursor</param>
		/// <param name="Offset">Initial offset</param>
		/// <param name="Pattern">Pattern to match</param>
		/// <returns>Offset of the line that matches the pattern (inclusive), or EOF is encountered</returns>
		public static int MatchForwardsUntil(this ILogCursor Cursor, int Offset, string Pattern)
		{
			string? NextLine;
			for (int NextOffset = Offset + 1; Cursor.TryGetLine(NextOffset, out NextLine); NextOffset++)
			{
				if (Regex.IsMatch(NextLine, Pattern))
				{
					return NextOffset;
				}
			}
			return Offset;
		}
	}
}
