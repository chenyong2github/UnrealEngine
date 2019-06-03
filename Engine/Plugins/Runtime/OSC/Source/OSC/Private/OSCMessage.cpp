// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCMessage.h"
#include "OSCStream.h"

FOSCMessagePacket::FOSCMessagePacket() 
	: FOSCPacket() 
{}

FOSCMessagePacket::~FOSCMessagePacket() 
{}


void FOSCMessagePacket::WriteData(FOSCStream& Stream)
{
	// Begin writing data 
	Stream.WriteString(Address);

	// write type tags
	FString tagTypes = ",";
	for (int i = 0; i < GetNumArguments(); i++)
	{
		tagTypes += static_cast<char>(GetArgument(i).GetTypeTag());
	}

	Stream.WriteString(tagTypes);

	for (FOSCType type : Arguments)
	{
		switch (type.GetTypeTag())
		{
		case EOSCTypeTag::CHAR:
			Stream.WriteChar(type.GetChar());
			break;
		case EOSCTypeTag::INT32:
			Stream.WriteInt32(type.GetInt32());
			break;
		case EOSCTypeTag::FLOAT:
			Stream.WriteFloat(type.GetFloat());
			break;
		case EOSCTypeTag::DOUBLE:
			Stream.WriteDouble(type.GetDouble());
			break;
		case EOSCTypeTag::INT64:
			Stream.WriteInt64(type.GetInt64());
			break;
		case EOSCTypeTag::TIME:
			Stream.WriteUInt64(type.GetTimeTag());
			break;
		case EOSCTypeTag::STRING:
			Stream.WriteString(type.GetString());
			break;
		case EOSCTypeTag::BLOB:
		{
			TArray<uint8> blob = type.GetBlob();
			Stream.WriteBlob(blob);
		}
		break;
		case EOSCTypeTag::COLOR:
			Stream.WriteInt32(type.GetColor().GetInt32());
			break;
		case EOSCTypeTag::TRUE:
		case EOSCTypeTag::FALSE:
		case EOSCTypeTag::NIL:
		case EOSCTypeTag::INFINITUM:
			// No values are written for these types
			break;
		default:
			// Argument is not supported 
			unimplemented();
			break;
		}
	}
}

void FOSCMessagePacket::ReadData(FOSCStream& Stream)
{
	// Read Address
	Address = Stream.ReadString();

	// Read string of tags
	TArray<TCHAR> tagTypes = Stream.ReadString().GetCharArray();

	// Skip the first argument which is ','
	for (int i=1; i<tagTypes.Num(); i++)
	{
		switch ((EOSCTypeTag)tagTypes[i])
		{
		case EOSCTypeTag::CHAR:
			AddArgument(FOSCType(Stream.ReadChar()));
			break;
		case EOSCTypeTag::INT32:
			AddArgument(FOSCType(Stream.ReadInt32()));
			break;
		case EOSCTypeTag::FLOAT:
			AddArgument(FOSCType(Stream.ReadFloat()));
			break;
		case EOSCTypeTag::DOUBLE:
			AddArgument(FOSCType(Stream.ReadDouble()));
			break;
		case EOSCTypeTag::INT64:
			AddArgument(FOSCType(Stream.ReadInt64()));
			break;
		case EOSCTypeTag::TRUE:
			AddArgument(FOSCType(true));
			break;
		case EOSCTypeTag::FALSE:
			AddArgument(FOSCType(false));
			break;
		case EOSCTypeTag::NIL:
			AddArgument(FOSCType(EOSCTypeTag::NIL));
			break;
		case EOSCTypeTag::INFINITUM:
			AddArgument(FOSCType(EOSCTypeTag::INFINITUM));
			break;
		case EOSCTypeTag::TIME:
			AddArgument(FOSCType(Stream.ReadUInt64()));
			break;
		case EOSCTypeTag::STRING:
			AddArgument(FOSCType(Stream.ReadString()));
			break;
		case EOSCTypeTag::BLOB:
			AddArgument(FOSCType(Stream.ReadBlob()));
			break;
		case EOSCTypeTag::COLOR:
			AddArgument(FOSCType(FOSCColor(Stream.ReadInt32())));
			break;
		case EOSCTypeTag::TERMINATE:
			Stream.ReadChar();
			break;

		default:
			// Argument is not supported 
			unimplemented();
			break;
		}
	}
}

const TSharedPtr<FOSCMessagePacket> FOSCMessage::GetOrCreatePacket()
{
	if (!Packet.IsValid())
	{
		Packet = MakeShareable(new FOSCMessagePacket());
	}

	return Packet;
}