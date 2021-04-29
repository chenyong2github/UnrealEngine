// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Parser;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace HordeAgent.Parser.Matchers
{
	/// <summary>
	/// Matches a generic C# exception
	/// </summary>
	class ExceptionEventMatcher : ILogEventMatcher
	{
		/// <inheritdoc/>
		public LogEvent? Match(ILogCursor Cursor, ILogContext Context)
		{
			if (Cursor.IsMatch(@"^\s*Unhandled Exception: "))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				while(Cursor.IsMatch(Builder.MaxOffset + 1, @"^\s*at "))
				{
					Builder.MaxOffset++;
				}
				return Builder.ToLogEvent(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.Exception);
			}
			return null;
		}
	}
}
