// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace BuildAgent.Run
{
	class LineBuffer
	{
		int LineNumber;

		string[] History;
		int HistoryIdx;
		int HistoryCount;

		List<string> NextLines;
		Func<string> ReadLine;

		public LineBuffer(Func<string> ReadLine, int HistorySize)
		{
			LineNumber = 1;
			History = new string[HistorySize];
			NextLines = new List<string>();

			this.ReadLine = ReadLine;
		}

		public int CurrentLineNumber
		{
			get { return LineNumber; }
		}

		public void Advance(int Count)
		{
			for(int Idx = 0; Idx < Count; Idx++)
			{
				MoveNext();
			}
		}

		public void MoveNext()
		{
			if(NextLines.Count == 0)
			{
				NextLines.Add(ReadLine());
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

		public bool TryGetLine(int Offset, out string NextLine)
		{
			NextLine = this[Offset];
			return NextLine != null;
		}

		public string this[int Offset]
		{
			get
			{
				if (Offset >= 0)
				{
					// Add new lines to the buffer
					while (Offset >= NextLines.Count)
					{
						NextLines.Add(ReadLine());
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
					throw new ArgumentException(String.Format("Invalid line buffer offset ({0})", Offset));
				}
			}
		}

		public override string ToString()
		{
			return String.Join("\n", NextLines);
		}
	}

	class ReadOnlyLineBuffer
	{
		LineBuffer Inner;

		public ReadOnlyLineBuffer(LineBuffer Inner)
		{
			this.Inner = Inner;
		}

		public string CurrentLine
		{
			get { return Inner[0]; }
		}

		public int CurrentLineNumber
		{
			get { return Inner.CurrentLineNumber; }
		}

		public string this[int Offset]
		{
			get { return Inner[Offset]; }
		}

		public bool IsMatch(string Pattern)
		{
			return IsMatch(0, Pattern);
		}

		public bool IsMatch(int Offset, string Pattern)
		{
			string Line;
			return TryGetLine(Offset, out Line) && Regex.IsMatch(Line, Pattern);
		}

		public bool TryMatch(string Pattern, out Match OutMatch)
		{
			return TryMatch(0, Pattern, out OutMatch);
		}

		public bool TryMatch(int Offset, string Pattern, out Match OutMatch)
		{
			string Line;
			if(!TryGetLine(Offset, out Line))
			{
				OutMatch = null;
				return false;
			}

			Match Match = Regex.Match(Line, Pattern);
			if(!Match.Success)
			{
				OutMatch = null;
				return false;
			}

			OutMatch = Match;
			return true;
		}

		public int MatchBackwards(int StartOffset, string Pattern)
		{
			int Offset = StartOffset;
			while(IsMatch(Offset - 1, Pattern))
			{
				Offset--;
			}
			return Offset;
		}

		public int MatchForwards(int Offset, string Pattern)
		{
			while(IsMatch(Offset + 1, Pattern))
			{
				Offset++;
			}
			return Offset;
		}

		public int MatchForwardsUntil(int Offset, string Pattern)
		{
			string NextLine;
			for (int NextOffset = Offset + 1; TryGetLine(NextOffset, out NextLine); NextOffset++)
			{
				if(Regex.IsMatch(NextLine, Pattern))
				{
					return NextOffset;
				}
			}
			return Offset;
		}

		public bool TryGetLine(int Offset, out string NextLine)
		{
			return Inner.TryGetLine(Offset, out NextLine);
		}

		public override string ToString()
		{
			return CurrentLine;
		}
	}
}
