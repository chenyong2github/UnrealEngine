// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Horde.Agent.Parser;
using Horde.Agent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Horde.Agent.Parser.Matchers
{
	/// <summary>
	/// Matches a generic C# exception
	/// </summary>
	class ExceptionEventMatcher : ILogEventMatcher
	{
		/// <inheritdoc/>
		public LogEventMatch? Match(ILogCursor cursor)
		{
			if (cursor.IsMatch(@"^\s*Unhandled Exception: "))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				while(builder.Current.IsMatch(1, @"^\s*at "))
				{
					builder.MoveNext();
				}
				return builder.ToMatch(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.Exception);
			}
			return null;
		}
	}
}
