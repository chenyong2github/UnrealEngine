// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Parser;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HordeAgent.Parser.Matchers
{
	class SystemicEventMatcher : ILogEventMatcher
	{
		public LogEvent? Match(ILogCursor Cursor, ILogContext Context)
		{
			if (Cursor.IsMatch(@"LogDerivedDataCache: .*queries/writes will be limited"))
			{
				return new LogEventBuilder(Cursor).ToLogEvent(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_SlowDDC);
			}
			if (Cursor.IsMatch(@"^\s*ERROR: Error: EC_OK"))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				while (Builder.IsMatch(Builder.MaxOffset + 1, @"^\s*ERROR:\s*$"))
				{
					Builder.MaxOffset++;
				}
				return new LogEventBuilder(Cursor).ToLogEvent(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_PdbUtil);
			}
			return null;
		}
	}
}
