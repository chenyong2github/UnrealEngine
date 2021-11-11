// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace EpicGames.Tracing.UnrealInsights.Events
{
	public class LoggingLogMessage : ITraceEvent
	{
		public static readonly EventType EventType;

		public ushort Size => throw new NotImplementedException();
		public EventType Type => EventType;

		static LoggingLogMessage()
		{
			EventType = new EventType("Logging", "LogMessage", EventType.FlagMaybeHasAux | EventType.FlagNoSync);
			EventType.AddEventType(0, 8, EventTypeField.TypeInt64, "LogPoint");
			EventType.AddEventType(8, 8, EventTypeField.TypeInt64, "Cycle");
			EventType.AddEventType(16, 0, EventTypeField.TypeArray, "FormatArgs");
		}
		
		public void Serialize(ushort Uid, BinaryWriter Writer)
		{
			throw new NotImplementedException();
		}
	}
}