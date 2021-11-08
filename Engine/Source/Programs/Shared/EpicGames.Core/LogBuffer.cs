// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Internal implementation of <see cref="ILogCursor"/> used to parse events
	/// </summary>
	class LogBuffer : ILogCursor
	{
		int LineNumber;

		string?[] History;
		int HistoryIdx;
		int HistoryCount;

		List<string?> NextLines;

		public LogBuffer(int HistorySize)
		{
			LineNumber = 1;
			History = new string?[HistorySize];
			NextLines = new List<string?>();
		}

		public bool NeedMoreData
		{
			get;
			private set;
		}

		public string? CurrentLine
		{
			get { return this[0]; }
		}

		public int CurrentLineNumber
		{
			get { return LineNumber; }
		}

		public int Length
		{
			get { return NextLines.Count; }
		}

		public void AddLine(string? Line)
		{
			NextLines.Add(Line);
			NeedMoreData = false;
		}

		public void Advance(int Count)
		{
			for (int Idx = 0; Idx < Count; Idx++)
			{
				MoveNext();
			}
		}

		public void MoveNext()
		{
			if (NextLines.Count == 0)
			{
				throw new InvalidOperationException("Attempt to move past end of line buffer");
			}

			HistoryIdx++;
			if (HistoryIdx >= History.Length)
			{
				HistoryIdx = 0;
			}
			if (HistoryCount < History.Length)
			{
				HistoryCount++;
			}

			History[HistoryIdx] = NextLines[0];
			NextLines.RemoveAt(0);

			LineNumber++;
		}

		public string? this[int Offset]
		{
			get
			{
				if (Offset >= 0)
				{
					// Add new lines to the buffer
					if (Offset >= NextLines.Count)
					{
						NeedMoreData = true;
						return null;
					}
					return NextLines[Offset];
				}
				else if (Offset >= -HistoryCount)
				{
					// Retrieve a line from the history buffer
					int Idx = HistoryIdx + 1 + Offset;
					if (Idx < 0)
					{
						Idx += History.Length;
					}
					return History[Idx];
				}
				else
				{
					// Invalid index
					return null;
				}
			}
		}

		public override string? ToString()
		{
			return CurrentLine;
		}
	}
}
