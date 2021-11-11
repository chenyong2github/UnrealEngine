// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace EpicGames.Tracing.UnrealInsights.Events
{
	public class DiagnosticsSession2Event : ITraceEvent
	{
		public static readonly EventType EventType;
		
		public ushort Size => (ushort) (GenericEvent.Size + TraceImportantEventHeader.HeaderSize);
		public EventType Type => EventType;
		private readonly GenericEvent GenericEvent;

		public DiagnosticsSession2Event(string Platform, string AppName, string CommandLine, string Branch, string BuildVersion, int ChangeList, int ConfigurationType, int TargetType)
		{
			GenericEvent.Field[] Fields =
			{
				GenericEvent.Field.FromString(Platform),
				GenericEvent.Field.FromString(AppName),
				GenericEvent.Field.FromString(CommandLine),
				GenericEvent.Field.FromString(Branch),
				GenericEvent.Field.FromString(BuildVersion),
				GenericEvent.Field.FromInt(ChangeList),
				GenericEvent.Field.FromInt(ConfigurationType),
				GenericEvent.Field.FromInt(TargetType),
			};

			GenericEvent = new GenericEvent(0, Fields, EventType);
		}
		
		static DiagnosticsSession2Event()
		{
			EventType = new EventType("Diagnostics", "Session2", EventType.FlagImportant | EventType.FlagMaybeHasAux | EventType.FlagNoSync);
			EventType.AddEventType(0, 0, EventTypeField.TypeAnsiString, "Platform");
			EventType.AddEventType(0, 0, EventTypeField.TypeAnsiString, "AppName");
			EventType.AddEventType(0, 0, EventTypeField.TypeWideString, "CommandLine");
			EventType.AddEventType(0, 0, EventTypeField.TypeWideString, "Branch");
			EventType.AddEventType(0, 0, EventTypeField.TypeWideString, "BuildVersion");
			EventType.AddEventType(0, 4, EventTypeField.TypeInt32, "Changelist");
			EventType.AddEventType(4, 1, EventTypeField.TypeInt8, "ConfigurationType");
			EventType.AddEventType(5, 1, EventTypeField.TypeInt8, "TargetType");
		}

		public void Serialize(ushort Uid, BinaryWriter Writer)
		{
			new TraceImportantEventHeader(Uid, GenericEvent.Size).Serialize(Writer);
			GenericEvent.Serialize(Uid, Writer);
		}
	}
}