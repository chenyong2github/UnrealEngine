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
	/// <summary>
	/// Matches events formatted as UE log channel output
	/// </summary>
	class LogChannelEventMatcher : ILogEventMatcher
	{
		/// <inheritdoc/>
		public LogEvent? Match(ILogCursor Input, ILogContext Context)
		{
			Match? Match;
			if(Input.TryMatch(@"^(\s*)(?<channel>[a-zA-Z_][a-zA-Z0-9_]*):\s*(?<severity>Error|Warning|Display): ", out Match))
			{
				string Indent = Match.Groups[1].Value;

				LogEventBuilder Builder = new LogEventBuilder(Input);

				LogEventLine FirstLine = Builder.Lines[0];
				FirstLine.AddSpan(Match.Groups["channel"]).MarkAsChannel();
				FirstLine.AddSpan(Match.Groups["severity"]).MarkAsSeverity();

				while (Input.IsMatch(Builder.MaxOffset + 1, $"^({Indent} | *$)"))
				{
					Builder.MaxOffset++;
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

				return Builder.ToLogEvent(LogEventPriority.Low, Level, KnownLogEvents.Engine_LogChannel);
			}
			return null;
		}
	}
}
