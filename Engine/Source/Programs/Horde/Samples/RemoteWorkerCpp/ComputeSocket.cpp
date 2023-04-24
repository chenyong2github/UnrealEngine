// Copyright Epic Games, Inc. All Rights Reserved.

#include <windows.h>
#include <iostream>
#include <assert.h>
#include "ComputeSocket.h"

const char* const FComputeSocket::EnvVarName = "UE_HORDE_COMPUTE_IPC";

FComputeSocket::FComputeSocket()
{
}

FComputeSocket::~FComputeSocket()
{
	Close();
}

bool FComputeSocket::Open()
{
	Close();

	char EnvVar[MAX_PATH];
	int Length = GetEnvironmentVariableA(EnvVarName, EnvVar, sizeof(EnvVar) / sizeof(EnvVar[0]));

	if (Length <= 0 || Length >= sizeof(EnvVar))
	{
		return false;
	}

	return CommandBuffer.OpenExisting(EnvVar);
}

void FComputeSocket::Close()
{
	CommandBuffer.Close();
}

void FComputeSocket::AttachRecvBuffer(int ChannelId, FSharedMemoryBuffer& Buffer)
{
	const int AttachRecvBufferType = 0;
	AttachBuffer(ChannelId, AttachRecvBufferType, Buffer);
}

void FComputeSocket::AttachSendBuffer(int ChannelId, FSharedMemoryBuffer& Buffer)
{
	const int AttachSendBufferType = 1;
	AttachBuffer(ChannelId, AttachSendBufferType, Buffer);
}

void FComputeSocket::AttachBuffer(int ChannelId, int Type, FSharedMemoryBuffer& Buffer)
{
	size_t Size;
	unsigned char* Data = CommandBuffer.GetWriteMemory(Size);

	size_t Len = 0;
	Len += WriteVarUInt(Data + Len, Type);
	Len += WriteVarUInt(Data + Len, (unsigned int)ChannelId);
	Len += WriteString(Data + Len, Buffer.GetName());

	CommandBuffer.AdvanceWritePosition(Len);
}

size_t FComputeSocket::WriteVarUInt(unsigned char* Pos, unsigned int Value)
{
	// Use BSR to return the log2 of the integer
	// return 0 if value is 0
	unsigned long BitIndex;
	unsigned int FloorLog2 = _BitScanReverse(&BitIndex, Value) ? BitIndex : 0;
	unsigned int ByteCount = (unsigned int)(int(FloorLog2) / 7 + 1);

	unsigned char* OutBytes = Pos + ByteCount - 1;
	switch (ByteCount - 1)
	{
	case 4: *OutBytes-- = (unsigned char)(Value); Value >>= 8;
	case 3: *OutBytes-- = (unsigned char)(Value); Value >>= 8;
	case 2: *OutBytes-- = (unsigned char)(Value); Value >>= 8;
	case 1: *OutBytes-- = (unsigned char)(Value); Value >>= 8;
	default: break;
	}
	*OutBytes = (unsigned char)(0xff << (9 - ByteCount)) | (unsigned char)(Value);

	return ByteCount;
}

size_t FComputeSocket::WriteString(unsigned char* Pos, const char* Text)
{
	unsigned int NameLen = (unsigned int)strlen(Text);
	size_t Len = WriteVarUInt(Pos, NameLen);
	memcpy(Pos + Len, Text, (unsigned int)NameLen);
	return Len + NameLen;
}

