// Copyright Epic Games, Inc. All Rights Reserved.

#include "Analysis/MetasoundFrontendVertexAnalyzerValue.h"

#include "Analysis/MetasoundFrontendAnalyzerAddress.h"
#include "MetasoundDataReference.h"
#include "MetasoundPrimitives.h"
#include "MetasoundRouter.h"
#include "MetasoundTrigger.h"


namespace Metasound
{
	namespace Frontend
	{
		METASOUND_DEFINE_VALUE_ANALYZER(FVertexAnalyzerBool, bool)
		METASOUND_DEFINE_VALUE_ANALYZER(FVertexAnalyzerFloat, float)
		METASOUND_DEFINE_VALUE_ANALYZER(FVertexAnalyzerInt, int32)
		METASOUND_DEFINE_VALUE_ANALYZER(FVertexAnalyzerString, FString)
	} // namespace Frontend
} // namespace Metasound
