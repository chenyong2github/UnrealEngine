// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Engine.h"
#include "Trace/Analysis.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
void FAnalysisProcessor::Start(IInDataStream& DataStream)
{
	FAnalysisEngine* Engine = (FAnalysisEngine*)Impl;
	if (Engine == nullptr)
	{
		return;
	}

	FStreamReader Reader(DataStream);
	while (FStreamReader::FData* Data = Reader.Read())
	{
		if (!Engine->OnData(*Data))
		{
			break;
		}
	}
	delete Engine;
	Engine = nullptr;
}

} // namespace Trace
