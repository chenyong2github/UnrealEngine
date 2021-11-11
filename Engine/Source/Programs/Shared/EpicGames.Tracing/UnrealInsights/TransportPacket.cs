// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace EpicGames.Tracing.UnrealInsights
{
	// Handles FTidPacketBase, TTidPacket, TTidPacketEncoded
	public class TransportPacket
	{
		public const int ThreadIdEvents = 0;
		public const int ThreadIdInternal = 1;
		public const int ThreadIdImportants = ThreadIdInternal;
		
		private const ushort EncodedMarker = 0x8000;
		private const ushort PartialMarker = 0x4000;
		private const ushort ThreadIdMask = PartialMarker - 1;
		
		public ushort PacketSize { get; private set; }
		public ushort ThreadId { get; private set; }
		public ushort DecodedSize { get; private set; }
		public byte[] Data { get; private set; } = Array.Empty<byte>();
	
		private TransportPacket()
		{
		}
		
		public bool IsEncoded()
		{
			return (ThreadId & EncodedMarker) != 0;
		}
		
		public bool IsPartial()
		{
			return (ThreadId & PartialMarker) != 0;
		}
		
		public ushort GetThreadId()
		{
			return (ushort) (ThreadId & ThreadIdMask);
		}
		
		public static bool IsNormalThread(ushort ThreadId)
		{
			if (ThreadId == ThreadIdEvents) return false;
			if (ThreadId == ThreadIdInternal) return false;
			if (ThreadId == ThreadIdImportants) return false;
			return true;
		}
		
		public void Serialize(BinaryWriter Writer, IEnumerable<(ushort, ITraceEvent)> Events)
		{
			ushort TotalSize = (ushort) Events.Sum(x =>
			{
				ITraceEvent Event = x.Item2;
				return Event.Size;
			});
			TotalSize += 4; // The two uint16 writes below are included
			
			Writer.Write(TotalSize);
			Writer.Write(ThreadId);

			foreach ((ushort Uid, ITraceEvent Event) in Events)
			{
				Event.Serialize(Uid, Writer);
			}
		}

		public override string ToString()
		{
			return $"TransportPacket(ThreadId={GetThreadId()} PacketSize={PacketSize} IsEncoded={IsEncoded()} IsPartial={IsPartial()})";
		}

		public static TransportPacket Create(ushort PacketSize, ushort ThreadId)
		{
			TransportPacket Packet = new TransportPacket();
			Packet.PacketSize = PacketSize;
			Packet.ThreadId = ThreadId;
			return Packet;
		}

		public static TransportPacket Deserialize(BinaryReader Reader)
		{
			TransportPacket Packet = new TransportPacket();
			Packet.PacketSize = Reader.ReadUInt16();
			Packet.ThreadId = Reader.ReadUInt16();

			int HeaderSize = sizeof(ushort) + sizeof(ushort); // PacketSize + ThreadId
			if (Packet.IsEncoded())
			{
				Packet.DecodedSize = Reader.ReadUInt16();
				HeaderSize += sizeof(ushort); // sizeof(DecodedSize) 
			}

			int BytesToRead = Packet.PacketSize - HeaderSize;
			Packet.Data = Reader.ReadBytesStrict(BytesToRead);
			return Packet;
		}
	}
}