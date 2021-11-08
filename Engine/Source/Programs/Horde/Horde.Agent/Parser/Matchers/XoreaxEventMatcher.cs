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
	class XoreaxEventMatcher : ILogEventMatcher
	{
		public LogEventMatch? Match(ILogCursor Cursor)
		{
			if (Cursor.IsMatch(@"\(BuildService.exe\) is not running"))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				return Builder.ToMatch(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_Xge_ServiceNotRunning);
			}

			if (Cursor.IsMatch(@"^\s*--------------------Build System Warning[- ]"))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				if (Builder.Next.TryMatch(@"^(\s*)([^ ].*):", out Match? Prefix))
				{
					Builder.MoveNext();

					string Message = Prefix.Groups[2].Value;

					EventId EventId = KnownLogEvents.Systemic_Xge;
					if (Regex.IsMatch(Message, "Failed to connect to Coordinator"))
					{
						EventId = KnownLogEvents.Systemic_Xge_Standalone;
					}

					string PrefixPattern = $"^{Prefix.Groups[1].Value}\\s";
					while (Builder.Next.IsMatch(PrefixPattern))
					{
						Builder.MoveNext();
					}

					return Builder.ToMatch(LogEventPriority.High, LogLevel.Information, EventId);
				}
			}
			return null;
		}
	}
}
