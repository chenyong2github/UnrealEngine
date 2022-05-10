// Copyright Epic Games, Inc. All Rights Reserved.
#include "Analysis/MetasoundFrontendAnalyzerView.h"

#include "Analysis/MetasoundFrontendVertexAnalyzer.h"
#include "Analysis/MetasoundFrontendVertexAnalyzerEnvelopeFollower.h"
#include "Analysis/MetasoundFrontendGraphAnalyzer.h"
#include "Math/UnrealMathUtility.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontendController.h"
#include "MetasoundOperatorSettings.h"
#include "Templates/Function.h"


namespace Metasound
{
	namespace Frontend
	{
		FMetasoundAnalyzerView::FMetasoundAnalyzerView(const FOperatorSettings& InOperatorSettings, FAnalyzerAddress&& InAnalyzerAddress)
			: AnalyzerAddress(MoveTemp(InAnalyzerAddress))
		{
			const FSendAddress SendAddress = AnalyzerAddress.ToSendAddress();
			const FReceiverInitParams ReceiverParams { InOperatorSettings };
			Receiver = FDataTransmissionCenter::Get().RegisterNewReceiver(SendAddress, ReceiverParams);
		}

		IReceiver& FMetasoundAnalyzerView::GetReceiverChecked()
		{
			check(Receiver.IsValid());
			return *Receiver;
		}

		const IReceiver& FMetasoundAnalyzerView::GetReceiverChecked() const
		{
			check(Receiver.IsValid());
			return *Receiver;
		}
	} // namespace Frontend
} // namespace Metasound
