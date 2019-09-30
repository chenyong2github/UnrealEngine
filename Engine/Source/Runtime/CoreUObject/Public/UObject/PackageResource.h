// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FSimpleArchive
	: public FArchive
{
public:
	FSimpleArchive(const uint8* BufferPtr, uint64 BufferSize)
	{
#if DEVIRTUALIZE_FLinkerLoad_Serialize
		ActiveFPLB->OriginalFastPathLoadBuffer = BufferPtr;
		ActiveFPLB->StartFastPathLoadBuffer = BufferPtr;
		ActiveFPLB->EndFastPathLoadBuffer = BufferPtr + BufferSize;
#endif
	}

	int64 TotalSize() override
	{
#if DEVIRTUALIZE_FLinkerLoad_Serialize
		return ActiveFPLB->EndFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer;
#else
		return 0;
#endif
	}

	int64 Tell() override
	{
#if DEVIRTUALIZE_FLinkerLoad_Serialize
		return ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer;
#else
		return 0;
#endif
	}

	void Seek(int64 Position) override
	{
#if DEVIRTUALIZE_FLinkerLoad_Serialize
		ActiveFPLB->StartFastPathLoadBuffer = ActiveFPLB->OriginalFastPathLoadBuffer + Position;
		check(ActiveFPLB->StartFastPathLoadBuffer <= ActiveFPLB->EndFastPathLoadBuffer);
#endif
	}

	void Serialize(void* Data, int64 Length) override
	{
#if DEVIRTUALIZE_FLinkerLoad_Serialize
		if (!Length || ArIsError)
		{
			return;
		}
		check(ActiveFPLB->StartFastPathLoadBuffer + Length <= ActiveFPLB->EndFastPathLoadBuffer);
		FMemory::Memcpy(Data, ActiveFPLB->StartFastPathLoadBuffer, Length);
		ActiveFPLB->StartFastPathLoadBuffer += Length;
#endif
	}
};
