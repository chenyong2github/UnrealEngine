// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Analysis/MetasoundFrontendVertexAnalyzer.h"
#include "Containers/Map.h"
#include "HAL/Platform.h"
#include "MetasoundAssetBase.h"
#include "MetasoundRouter.h"
#include "Templates/UniquePtr.h"


// Forward Declarations
class FMetasoundAssetBase;
class UAudioComponent;

namespace Metasound
{
	namespace Frontend
	{
		// Pairs an IReceiver with a given AnalyzerAddress, which enables
		// watching a particular analyzer result on any given thread.
		class METASOUNDFRONTEND_API FMetasoundAnalyzerView
		{
			TUniquePtr<IReceiver> Receiver;

		public:
			const FAnalyzerAddress AnalyzerAddress = { };

			FMetasoundAnalyzerView() = default;
			FMetasoundAnalyzerView(const FOperatorSettings& InOperatorSettings, FAnalyzerAddress&& InAnalyzerAddress);

			IReceiver& GetReceiverChecked();
			const IReceiver& GetReceiverChecked() const;
		};
	}
}
