// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
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
		public LogEventMatch? Match(ILogCursor Cursor)
		{
			if (Cursor.IsMatch(@"LogDerivedDataCache: .*queries/writes will be limited"))
			{
				return new LogEventBuilder(Cursor).ToMatch(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_SlowDDC);
			}
			if (Cursor.IsMatch(@"^\s*ERROR: Error: EC_OK"))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				while (Builder.Next.IsMatch(@"^\s*ERROR:\s*$"))
				{
					Builder.MoveNext();
				}
				return new LogEventBuilder(Cursor).ToMatch(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_PdbUtil);
			}
			return null;
		}
	}
}
