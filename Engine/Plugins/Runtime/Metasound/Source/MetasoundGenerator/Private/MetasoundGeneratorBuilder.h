// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundGenerator.h"

#include "Async/AsyncWork.h"
#include "Containers/Map.h"
#include "MetasoundEnvironment.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundOperatorCache.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundVertex.h"
#include "MetasoundVertexData.h"
#include "Misc/Guid.h"
#include "Templates/UniquePtr.h"

namespace Metasound
{
	class FMetasoundGenerator;

	class FAsyncMetaSoundBuilder : public FNonAbandonableTask
	{
	public:
		FAsyncMetaSoundBuilder(FMetasoundGenerator* InGenerator, FMetasoundGeneratorInitParams&& InInitParams, bool bInTriggerGenerator);

		~FAsyncMetaSoundBuilder() = default;

		void DoWork();

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncMetaSoundBuilder, STATGROUP_ThreadPoolAsyncTasks);
		}

	private:

		FMetasoundGenerator* Generator;
		FMetasoundGeneratorInitParams InitParams;
		bool bTriggerGenerator;
	};

	namespace GeneratorBuilder
	{
		TArray<FAudioBufferReadRef> FindOutputAudioBuffers(const TArray<FVertexName>& InAudioVertexNames, const FVertexInterfaceData& InVertexData, const FOperatorSettings& InOperatorSettings, const FString& InMetaSoundName);

		void LogBuildErrors(const FString& InMetaSoundName, const FBuildResults& InBuildResults);

		TUniquePtr<Frontend::FGraphAnalyzer> BuildGraphAnalyzer(TMap<FGuid, FDataReferenceCollection>&& InInternalDataReferences, const FMetasoundEnvironment& InEnvironment, const FOperatorSettings& InOperatorSettings);

		FOperatorAndInputs BuildGraphOperator(FMetasoundGeneratorInitParams& InInitParams, FBuildResults& OutBuildResults);

		MetasoundGeneratorPrivate::FMetasoundGeneratorData BuildGeneratorData(const FMetasoundGeneratorInitParams& InInitParams, FOperatorAndInputs&& InGraphOperatorAndInputs, TUniquePtr<Frontend::FGraphAnalyzer> InAnalyzer);

		void ApplyAudioParameters(const FOperatorSettings& InOperatorSettings, TArray<FAudioParameter>&& InParameters, FInputVertexInterfaceData& InInterface);
	}
}
