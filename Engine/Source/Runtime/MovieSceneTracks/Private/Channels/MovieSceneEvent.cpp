// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneEvent.h"

#if WITH_EDITORONLY_DATA
	#include "EdGraph/EdGraph.h"
	#include "K2Node_FunctionEntry.h"
#endif

#if WITH_EDITORONLY_DATA
void FMovieSceneEvent::PostSerialize(const FArchive& Ar)
{
	UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(FunctionEntry_DEPRECATED.Get());
	if (FunctionEntry)
	{
		// If the graph needs loading, load it
		if (FunctionEntry->GetGraph()->HasAnyFlags(RF_NeedLoad) && FunctionEntry->GetLinker())
		{
			FunctionEntry->GetLinker()->Preload(FunctionEntry->GetGraph());
		}
		GraphGuid = FunctionEntry->GetGraph()->GraphGuid;
	}
}
#endif
