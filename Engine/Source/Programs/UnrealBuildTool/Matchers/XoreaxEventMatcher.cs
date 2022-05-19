// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.RegularExpressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace UnrealBuildTool.Matchers
{
	class XoreaxEventMatcher : ILogEventMatcher
	{
		public LogEventMatch? Match(ILogCursor cursor)
		{
			if (cursor.IsMatch(@"\(BuildService.exe\) is not running"))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				return builder.ToMatch(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_Xge_ServiceNotRunning);
			}

			if (cursor.IsMatch(@"BUILD FAILED: (.*)xgConsole\.exe(.*)"))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				return builder.ToMatch(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Systemic_Xge_BuildFailed);
			}

			if (cursor.IsMatch(@"^\s*--------------------Build System Warning[- ]"))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				if (builder.Next.TryMatch(@"^(\s*)([^ ].*):", out Match? prefix))
				{
					builder.MoveNext();

					string message = prefix.Groups[2].Value;

					EventId eventId = KnownLogEvents.Systemic_Xge;
					if (Regex.IsMatch(message, "Failed to connect to Coordinator"))
					{
						eventId = KnownLogEvents.Systemic_Xge_Standalone;
					}

					string prefixPattern = $"^{prefix.Groups[1].Value}\\s";
					while (builder.Next.IsMatch(prefixPattern))
					{
						builder.MoveNext();
					}

					return builder.ToMatch(LogEventPriority.High, LogLevel.Information, eventId);
				}
			}
			return null;
		}
	}
}
