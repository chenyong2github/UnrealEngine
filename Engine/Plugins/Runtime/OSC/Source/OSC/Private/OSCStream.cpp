// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCStream.h"

FOSCStream::FOSCStream() : Data(nullptr), Size(0) { Position = 0; }
FOSCStream::~FOSCStream() {}
FOSCStream::FOSCStream(char* Data, int Size) 
{ 
	this->Data = Data;
	this->Size = Size;
	Position = 0; 
}

char FOSCStream::ReadChar()
{
	char temp;
	if (Read(&temp, 1) == 1)
	{
		return temp;
	}

	return 0;
}

void FOSCStream::WriteChar(char Char)
{
	Write(&Char, 1);
}

int32 FOSCStream::ReadInt32()
{
	char temp[4];
	if (Read(temp, 4) == 4)
	{
#if PLATFORM_LITTLE_ENDIAN
		union {
			int32 i;
			char c[4];
		} u;

		u.c[0] = temp[3];
		u.c[1] = temp[2];
		u.c[2] = temp[1];
		u.c[3] = temp[0];

		return u.i;
#else
		return *(int32*)temp;
#endif
	}

	return 0;
}

void FOSCStream::WriteInt32(int32 Value)
{
	char temp[4];

#ifdef PLATFORM_LITTLE_ENDIAN
	union {
		int32 i;
		char c[4];
	} u;

	u.i = Value;

	temp[3] = u.c[0];
	temp[2] = u.c[1];
	temp[1] = u.c[2];
	temp[0] = u.c[3];
#else
	*reinterpret_cast<int32*>(temp) = Value;
#endif
	Write(temp, 4);
}

double FOSCStream::ReadDouble()
{
	char temp[8];
	if (Read(temp, 8) == 8)
	{
#if PLATFORM_LITTLE_ENDIAN
		union {
			double d;
			char c[8];
		} u;

		u.c[0] = temp[7];
		u.c[1] = temp[6];
		u.c[2] = temp[5];
		u.c[3] = temp[4];
		u.c[4] = temp[3];
		u.c[5] = temp[2];
		u.c[6] = temp[1];
		u.c[7] = temp[0];

		return u.d;
#else
		return *(double*)temp;
#endif

	}

	return 0;
}

void FOSCStream::WriteDouble(uint64 Value)
{
	char temp[8];

#ifdef PLATFORM_LITTLE_ENDIAN
	union {
		double i;
		char c[8];
	} u;

	u.i = Value;

	temp[7] = u.c[0];
	temp[6] = u.c[1];
	temp[5] = u.c[2];
	temp[4] = u.c[3];
	temp[3] = u.c[4];
	temp[2] = u.c[5];
	temp[1] = u.c[6];
	temp[0] = u.c[7];
#else
	*reinterpret_cast<double*>(temp) = Value;
#endif
	Write(temp, 8);
}


int64 FOSCStream::ReadInt64()
{
	char temp[8];
	if (Read(temp, 8) == 8)
	{
#if PLATFORM_LITTLE_ENDIAN
		union {
			int64 i;
			char c[8];
		} u;

		u.c[0] = temp[7];
		u.c[1] = temp[6];
		u.c[2] = temp[5];
		u.c[3] = temp[4];
		u.c[4] = temp[3];
		u.c[5] = temp[2];
		u.c[6] = temp[1];
		u.c[7] = temp[0];

		return u.i;
#else
		return *(int64*)temp;
#endif
	}

	return 0;
}

void FOSCStream::WriteInt64(int64 Value)
{
	char temp[8];

#ifdef PLATFORM_LITTLE_ENDIAN
	union {
		int64 i;
		char c[8];
	} u;

	u.i = Value;

	temp[7] = u.c[0];
	temp[6] = u.c[1];
	temp[5] = u.c[2];
	temp[4] = u.c[3];
	temp[3] = u.c[4];
	temp[2] = u.c[5];
	temp[1] = u.c[6];
	temp[0] = u.c[7];
#else
	*reinterpret_cast<int64*>(temp) = Value;
#endif
	Write(temp, 8);
}

uint64 FOSCStream::ReadUInt64()
{
	char temp[8];
	if (Read(temp, 8) == 8)
	{
#if PLATFORM_LITTLE_ENDIAN
		union {
			uint64 i;
			char c[8];
		} u;

		u.c[0] = temp[7];
		u.c[1] = temp[6];
		u.c[2] = temp[5];
		u.c[3] = temp[4];
		u.c[4] = temp[3];
		u.c[5] = temp[2];
		u.c[6] = temp[1];
		u.c[7] = temp[0];

		return u.i;
#else
		return *(uint64*)temp;
#endif

	}

	return 0;
}

void FOSCStream::WriteUInt64(uint64 Value)
{
	char temp[8];

#ifdef PLATFORM_LITTLE_ENDIAN
	union {
		uint64 i;
		char c[8];
	} u;

	u.i = Value;

	temp[7] = u.c[0];
	temp[6] = u.c[1];
	temp[5] = u.c[2];
	temp[4] = u.c[3];
	temp[3] = u.c[4];
	temp[2] = u.c[5];
	temp[1] = u.c[6];
	temp[0] = u.c[7];
#else
	*reinterpret_cast<uint64*>(temp) = Value;
#endif
	Write(temp, 8);
}

float FOSCStream::ReadFloat()
{
	char temp[4];
	if (Read(temp, 4) == 4)
	{
#if PLATFORM_LITTLE_ENDIAN
		union {
			float f;
			char c[4];
		} u;

		u.c[0] = temp[3];
		u.c[1] = temp[2];
		u.c[2] = temp[1];
		u.c[3] = temp[0];

		return u.f;
#else
		return *(float*)temp;
#endif
	}

	return 0.0f;
}

void FOSCStream::WriteFloat(float Value)
{
	char temp[4];

#ifdef PLATFORM_LITTLE_ENDIAN
	union {
		float f;
		char c[4];
	} u;

	u.f = Value;

	temp[3] = u.c[0];
	temp[2] = u.c[1];
	temp[1] = u.c[2];
	temp[0] = u.c[3];
#else
	*reinterpret_cast<float*>(temp) = Value;
#endif
	Write(temp, 4);
}

FString FOSCStream::ReadString()
{
	int32 count = 0;
	for (int32 index = Position; Data[index] != 0; index++)
	{
		count++;
	}

	FString s(count, &Data[Position]);

	Position += count + 1; // increment Position
	Position = ((Position + 3) / 4) * 4; // padded

	return s;
}

void FOSCStream::WriteString(FString string)
{
	TArray<TCHAR> s = string.GetCharArray();

	const int32 count = s.Num();
	for (int32 index = 0; index < count; index++)
	{
		Data[Position + index] = s[index];
	}

	Position += count;

	const int32 numPaddingZeros = ((count + 3) / 4) * 4; // padded
	for (int32 i = 0; i < numPaddingZeros - count; i++)
	{
		WriteChar('\0');
	}
}

TArray<uint8> FOSCStream::ReadBlob()
{
	TArray<uint8> Blob;

	int blobsize = ReadInt32();

	for (int i = 0; i < blobsize; i++)
	{
		Blob.Add(ReadChar());
	}

	Position = ((Position + 3) / 4) * 4; // padded

	return Blob;
}

void FOSCStream::WriteBlob(TArray<uint8>& Blob)
{
	// Write Blob size
	WriteInt32(Blob.Num());

	for (int i = 0; i < Blob.Num(); i++)
	{
		Write(&Blob[i], 4);
	}
}

int FOSCStream::Read(void* Buffer, int ToRead)
{
	if (ToRead <= 0 || Position >= Size)
		return 0;

	int num = FMath::Min<int>(ToRead, Size - Position);
	if (num > 0)
	{
		memcpy(Buffer, Data + Position, num);

		Position += num;
	}

	return num;
}

int FOSCStream::Write(void* Buffer, int ToWrite)
{
	if (ToWrite <= 0)
		return 0;

	memcpy(Data + Position, Buffer, ToWrite);

	Position += ToWrite;

	return ToWrite;
}

