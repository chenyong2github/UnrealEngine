// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace EpicGames.Tracing.UnrealInsights.Events
{
	public class MemoryMemoryScopeEvent : ITraceEvent
	{
		public static readonly EventType EventType;
		
		public ushort Size => throw new NotImplementedException();
		public EventType Type => EventType;
		
		static MemoryMemoryScopeEvent()
		{
			EventType = new EventType("Memory", "MemoryScope", EventType.FlagNone);
			EventType.AddEventType(0, 4, EventTypeField.TypeInt32, "Id");
		}

		public void Serialize(ushort Uid, BinaryWriter Writer)
		{
			throw new NotImplementedException();
		}
	}
}