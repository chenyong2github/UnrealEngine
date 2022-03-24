// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;

namespace EpicGames.Core
{
	/// <summary>
	/// Internal implementation of <see cref="ILogCursor"/> used to parse events
	/// </summary>
	class LogBuffer : ILogCursor
	{
		int _lineNumber;
		readonly string?[] _history;
		int _historyIdx;
		int _historyCount;
		readonly List<string?> _nextLines;

		public LogBuffer(int historySize)
		{
			_lineNumber = 1;
			_history = new string?[historySize];
			_nextLines = new List<string?>();
		}

		public bool NeedMoreData
		{
			get;
			private set;
		}

		public string? CurrentLine => this[0];

		public int CurrentLineNumber => _lineNumber;

		public int Length => _nextLines.Count;

		public void AddLine(string? line)
		{
			_nextLines.Add(line);
			NeedMoreData = false;
		}

		public void Advance(int count)
		{
			for (int idx = 0; idx < count; idx++)
			{
				MoveNext();
			}
		}

		public void MoveNext()
		{
			if (_nextLines.Count == 0)
			{
				throw new InvalidOperationException("Attempt to move past end of line buffer");
			}

			_historyIdx++;
			if (_historyIdx >= _history.Length)
			{
				_historyIdx = 0;
			}
			if (_historyCount < _history.Length)
			{
				_historyCount++;
			}

			_history[_historyIdx] = _nextLines[0];
			_nextLines.RemoveAt(0);

			_lineNumber++;
		}

		public string? this[int offset]
		{
			get
			{
				if (offset >= 0)
				{
					// Add new lines to the buffer
					if (offset >= _nextLines.Count)
					{
						NeedMoreData = true;
						return null;
					}
					return _nextLines[offset];
				}
				else if (offset >= -_historyCount)
				{
					// Retrieve a line from the history buffer
					int idx = _historyIdx + 1 + offset;
					if (idx < 0)
					{
						idx += _history.Length;
					}
					return _history[idx];
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
