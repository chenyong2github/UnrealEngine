// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Logging/LogMacros.h"
#include "Containers/UnrealString.h"
#include "Trace/DataStream.h"

#include <memory.h>

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FFileStream
	: public IInDataStream
{
public:
					FFileStream(const TCHAR* FilePath);
	virtual			~FFileStream();
	virtual int32	Read(void* Data, uint32 Size) override;
	void			UpdateFileSize();

private:
	void			OpenFileInternal();

	FString			FilePath;
	IFileHandle*	Inner = nullptr;
	uint64			Cursor = 0;
	uint64			End = 0;
};

} // namespace Trace
