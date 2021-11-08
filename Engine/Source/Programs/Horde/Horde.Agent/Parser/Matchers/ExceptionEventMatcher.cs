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
using System.Threading.Tasks;

namespace HordeAgent.Parser.Matchers
{
	/// <summary>
	/// Matches a generic C# exception
	/// </summary>
	class ExceptionEventMatcher : ILogEventMatcher
	{
		/// <inheritdoc/>
		public LogEventMatch? Match(ILogCursor Cursor)
		{
			if (Cursor.IsMatch(@"^\s*Unhandled Exception: "))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				while(Builder.Current.IsMatch(1, @"^\s*at "))
				{
					Builder.MoveNext();
				}
				return Builder.ToMatch(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.Exception);
			}
			return null;
		}
	}
}
