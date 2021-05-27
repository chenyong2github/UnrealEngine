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
	class XoreaxEventMatcher : ILogEventMatcher
	{
		public LogEvent? Match(ILogCursor Cursor, ILogContext Context)
		{
			if (Cursor.IsMatch(@"\(BuildService.exe\) is not running"))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				return Builder.ToLogEvent(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_Xge_ServiceNotRunning);
			}

			if (Cursor.IsMatch(@"^\s*--------------------Build System Warning[- ]"))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				if (Builder.TryMatch(@"^(\s*)([^ ].*):", out Match? Prefix))
				{
					Builder.MaxOffset++;

					string Message = Prefix.Groups[2].Value;

					EventId EventId = KnownLogEvents.Systemic_Xge;
					if (Regex.IsMatch(Message, "Failed to connect to Coordinator"))
					{
						EventId = KnownLogEvents.Systemic_Xge_Standalone;
					}

					string PrefixPattern = $"^{Prefix.Groups[1].Value}\\s";
					while (Builder.IsMatch(PrefixPattern))
					{
						Builder.MaxOffset++;
					}

					return Builder.ToLogEvent(LogEventPriority.High, LogLevel.Information, EventId);
				}
			}
			return null;
		}
	}
}
