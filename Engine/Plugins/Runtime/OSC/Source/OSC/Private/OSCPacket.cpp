// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCPacket.h"

#include "OSCMessage.h"
#include "OSCBundle.h"
#include "OSCLog.h"


FOSCPacket::FOSCPacket()
{
}

FOSCPacket::~FOSCPacket()
{
}

TSharedPtr<FOSCPacket> FOSCPacket::CreatePacket(const uint8* PacketType)
{
	const FOSCAddress Address(ANSI_TO_TCHAR((const ANSICHAR*)&PacketType[0]));
	if (Address.IsMessage())
	{
		return MakeShareable(new FOSCMessagePacket());
	}
	else if (Address.IsBundle())
	{
		return MakeShareable(new FOSCBundlePacket());
	}
	else
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to parse lead character of OSC message packet. "
			"Lead identifier of '%c' not valid bundle (#) or message (/) identifier."), PacketType[0]);
		return nullptr;
	}
}

FOSCMessagePacket::FOSCMessagePacket()
	: FOSCPacket()
{}

FOSCMessagePacket::~FOSCMessagePacket()
{}

void FOSCMessagePacket::SetAddress(const FOSCAddress& InAddress)
{
	Address = InAddress;
}

const FOSCAddress& FOSCMessagePacket::GetAddress() const
{
	return Address;
}

TArray<FOSCType>& FOSCMessagePacket::GetArguments()
{
	return Arguments;
}

bool FOSCMessagePacket::IsBundle()
{
	return false;
}

bool FOSCMessagePacket::IsMessage()
{
	return true;
}

void FOSCMessagePacket::WriteData(FOSCStream& Stream)
{
	if (!Address.IsMessage())
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to write OSCMessagePacket. Invalid OSCAddress '%s'"), *Address.Value);
		return;
	}

	// Begin writing data 
	Stream.WriteString(Address.Value);

	// write type tags
	FString TagTypes = ",";
	for (int32 i = 0; i < Arguments.Num(); i++)
	{
		TagTypes += static_cast<ANSICHAR>(Arguments[i].GetTypeTag());
	}

	Stream.WriteString(TagTypes);

	for (FOSCType OSCType : Arguments)
	{
		switch (OSCType.GetTypeTag())
		{
		case EOSCTypeTag::CHAR:
			Stream.WriteChar(OSCType.GetChar());
			break;
		case EOSCTypeTag::INT32:
			Stream.WriteInt32(OSCType.GetInt32());
			break;
		case EOSCTypeTag::FLOAT:
			Stream.WriteFloat(OSCType.GetFloat());
			break;
		case EOSCTypeTag::DOUBLE:
			Stream.WriteDouble(OSCType.GetDouble());
			break;
		case EOSCTypeTag::INT64:
			Stream.WriteInt64(OSCType.GetInt64());
			break;
		case EOSCTypeTag::TIME:
			Stream.WriteUInt64(OSCType.GetTimeTag());
			break;
		case EOSCTypeTag::STRING:
			Stream.WriteString(OSCType.GetString());
			break;
		case EOSCTypeTag::BLOB:
		{
			TArray<uint8> blob = OSCType.GetBlob();
			Stream.WriteBlob(blob);
		}
		break;
		case EOSCTypeTag::COLOR:
#if PLATFORM_LITTLE_ENDIAN
			Stream.WriteInt32(OSCType.GetColor().ToPackedABGR());
#else
			Stream.WriteInt32(OSCType.GetColor().ToPackedRGBA());
#endif
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
	const FString StreamString = Stream.ReadString();
	const TArray<TCHAR>& TagTypes = StreamString.GetCharArray();

	// Skip the first argument which is ','
	for (int32 i = 1; i < TagTypes.Num(); i++)
	{
		const EOSCTypeTag Tag = static_cast<EOSCTypeTag>(TagTypes[i]);
		switch (Tag)
		{
		case EOSCTypeTag::CHAR:
			Arguments.Add(FOSCType(Stream.ReadChar()));
			break;
		case EOSCTypeTag::INT32:
			Arguments.Add(FOSCType(Stream.ReadInt32()));
			break;
		case EOSCTypeTag::FLOAT:
			Arguments.Add(FOSCType(Stream.ReadFloat()));
			break;
		case EOSCTypeTag::DOUBLE:
			Arguments.Add(FOSCType(Stream.ReadDouble()));
			break;
		case EOSCTypeTag::INT64:
			Arguments.Add(FOSCType(Stream.ReadInt64()));
			break;
		case EOSCTypeTag::TRUE:
			Arguments.Add(FOSCType(true));
			break;
		case EOSCTypeTag::FALSE:
			Arguments.Add(FOSCType(false));
			break;
		case EOSCTypeTag::NIL:
			Arguments.Add(FOSCType(EOSCTypeTag::NIL));
			break;
		case EOSCTypeTag::INFINITUM:
			Arguments.Add(FOSCType(EOSCTypeTag::INFINITUM));
			break;
		case EOSCTypeTag::TIME:
			Arguments.Add(FOSCType(Stream.ReadUInt64()));
			break;
		case EOSCTypeTag::STRING:
			Arguments.Add(FOSCType(Stream.ReadString()));
			break;
		case EOSCTypeTag::BLOB:
			Arguments.Add(FOSCType(Stream.ReadBlob()));
			break;
		case EOSCTypeTag::COLOR:
			Arguments.Add(FOSCType(FColor(Stream.ReadInt32())));
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
