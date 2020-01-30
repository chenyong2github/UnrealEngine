// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Run
{
	enum ErrorSeverity
	{
		Error,
		Warning,
		Silent,
	}

	enum ErrorPriority
	{
		Lowest,
		Low,
		BelowNormal,
		Normal,
		AboveNormal,
		High,
		Highest,
	}

	class ErrorMatch
	{
		public ErrorSeverity Severity;
		public ErrorPriority Priority;
		public string Type;
		public int MinLineNumber;
		public int MaxLineNumber;
		public List<string> Lines = new List<string>();
		public Dictionary<string, string> Properties = new Dictionary<string, string>(StringComparer.Ordinal);

		public ErrorMatch(ErrorSeverity Severity, ErrorPriority Priority, string Type, int MinLineNumber, int MaxLineNumber)
		{
			this.Severity = Severity;
			this.Priority = Priority;
			this.Type = Type;
			this.MinLineNumber = MinLineNumber;
			this.MaxLineNumber = MaxLineNumber;
		}

		public ErrorMatch(ErrorSeverity Severity, ErrorPriority Priority, string Type, ReadOnlyLineBuffer Input)
			: this(Severity, Priority, Type, Input, 0, 0)
		{
		}

		public ErrorMatch(ErrorSeverity Severity, ErrorPriority Priority, string Type, ReadOnlyLineBuffer Input, int MinOffset, int MaxOffset)
			: this(Severity, Priority, Type, Input.CurrentLineNumber + MinOffset, Input.CurrentLineNumber + MaxOffset)
		{
			for (int Offset = MinOffset; Offset <= MaxOffset; Offset++)
			{
				Lines.Add(Input[Offset]);
			}
		}
	}
}
