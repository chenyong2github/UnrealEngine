// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System.Text.RegularExpressions;

#nullable enable

namespace AutomationUtils.Matchers
{
	class SystemicEventMatcher : ILogEventMatcher
	{
		static readonly Regex s_ddc = new Regex(@"LogDerivedDataCache: .*queries/writes will be limited");

		static readonly Regex s_pdbUtil = new Regex(@"^\s*ERROR: Error: EC_OK");
		static readonly Regex s_pdbUtilSuffix = new Regex(@"^\s*ERROR:\s*$");

		static readonly Regex s_roboMerge = new Regex(@"RoboMerge\/gates.*already locked on Commit Server by buildmachine");

		public LogEventMatch? Match(ILogCursor cursor)
		{
			if (cursor.IsMatch(s_ddc))
			{
				return new LogEventBuilder(cursor).ToMatch(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_SlowDDC);
			}
			if (cursor.IsMatch(s_pdbUtil))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				while (builder.Next.IsMatch(s_pdbUtilSuffix))
				{
					builder.MoveNext();
				}
				return new LogEventBuilder(cursor).ToMatch(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_PdbUtil);
			}
			if (cursor.IsMatch(s_roboMerge))
			{
				return new LogEventBuilder(cursor).ToMatch(LogEventPriority.Low, LogLevel.Information, KnownLogEvents.Systemic_RoboMergeGateLocked);
			}
			return null;
		}
	}
}
