// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Analysis/MetasoundFrontendVertexAnalyzer.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundRouter.h"
#include "Templates/UniquePtr.h"


namespace Metasound
{
	namespace Frontend
	{
		// Handles intrinsic analysis operations within a given graph
		// should the graph's operator be enabled for analysis.
		class METASOUNDFRONTEND_API FGraphAnalyzer
		{
			const FOperatorSettings OperatorSettings;
			const uint64 InstanceID = INDEX_NONE;

			TUniquePtr<IReceiver> ActiveAnalyzerReceiver;
			TArray<TUniquePtr<Frontend::IVertexAnalyzer>> Analyzers;
			FNodeVertexDataMap InternalDataReferences;

		public:
			FGraphAnalyzer(const FOperatorSettings& InSettings, uint64 InInstanceID, FNodeVertexDataMap&& InGraphReferences);
			~FGraphAnalyzer() = default;

			// Creates a send channel name unique for the given sound instance used to send array of analyzer
			// addresses to the profiler of what analyzers are expected to be active for a given instance.
			static const FName GetAnalyzerArraySendChannelName(uint64 InInstanceID)
			{
				const FString ChannelName = FString::Printf(TEXT("AnalyzerArray%s%lld"), *FAnalyzerAddress::PathSeparator, InInstanceID);
				return *ChannelName;
			}

			// Execute analysis for the current block
			void Execute();
		};
	} // namespace Frontend
} // namespace Metasound
