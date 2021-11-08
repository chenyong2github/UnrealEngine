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
	/// <summary>
	/// Matches events formatted as UE log channel output
	/// </summary>
	class LogChannelEventMatcher : ILogEventMatcher
	{
		/// <inheritdoc/>
		public LogEventMatch? Match(ILogCursor Input)
		{
			Match? Match;
			if(Input.TryMatch(@"^(\s*)(?<channel>[a-zA-Z_][a-zA-Z0-9_]*):\s*(?<severity>Error|Warning|Display): ", out Match))
			{
				string Indent = Match.Groups[1].Value;

				LogEventBuilder Builder = new LogEventBuilder(Input);
				Builder.Annotate(Match.Groups["channel"], LogEventMarkup.Channel);
				Builder.Annotate(Match.Groups["severity"], LogEventMarkup.Severity);

				while (Builder.Next.IsMatch(1, $"^({Indent} | *$)"))
				{
					Builder.MoveNext();
				}

				LogLevel Level;
				switch(Match.Groups["severity"].Value)
				{
					case "Error":
						Level = LogLevel.Error;
						break;
					case "Warning":
						Level = LogLevel.Warning;
						break;
					default:
						Level = LogLevel.Information;
						break;
				}

				return Builder.ToMatch(LogEventPriority.Low, Level, KnownLogEvents.Engine_LogChannel);
			}
			return null;
		}
	}
}
