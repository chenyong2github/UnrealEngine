// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class FOSCStream
{
public:
	FOSCStream(char* Data, int Size);
	FOSCStream();
	virtual ~FOSCStream();

	/** Get stream bufer. */
	const char* GetBuffer() { return Data; }
	
	/** Get stream length */
	int GetLength() { return Position; }
	
	/** Returns true if stream has reached the end. */
	bool HasReachedEnd(){ return !(Position < Size); }
	
	/** Get current stream position. */
	int GetPosition() { return Position; }

	/** Set stream position. */
	void SetPosition(int InPosition) { this->Position = InPosition; }

	/** Read Char from the stream. */
	char ReadChar();

	/** Write Char into the stream. */
	void WriteChar(char Char);

	/** Read Int32 from the stream. */
	int32 ReadInt32();

	/** Write Int32 into the stream. */
	void WriteInt32(int32 Value);
	
	/** Read Double from the stream. */
	double ReadDouble();

	/** Write Double into the stream. */
	void WriteDouble(uint64 Value);
	
	/** Read Int64 from the stream. */
	int64 ReadInt64();

	/** Write Int64 into the stream. */
	void WriteInt64(int64 Value);

	/** Read UInt64 from the stream. */
	uint64 ReadUInt64();

	/** Write UInt64 into the stream. */
	void WriteUInt64(uint64 Value);

	/** Read Float from the stream. */
	float ReadFloat();
		
	/** Write Int64 into the stream. */
	void WriteFloat(float Value);
	
	/** Read String from the stream. */
	FString ReadString();

	/** Write String into the stream. */
	void WriteString(FString String);
	
	/** Read Blob from the stream. */
	TArray<uint8> ReadBlob();

	/** Write Blob into the stream. */
	void WriteBlob(TArray<uint8>& Blob);

private:
	
	/** Read data rom a buffer. */
	int Read(void* Buffer, int ToRead);
	
	/** Write data into buffer. */
	int Write(void* Buffer, int ToWrite);

	/** Stream data. */
	char* Data;

	/** Current buffer position. */
	int Position;

	/** Stream size. */
	int32 Size;
};

