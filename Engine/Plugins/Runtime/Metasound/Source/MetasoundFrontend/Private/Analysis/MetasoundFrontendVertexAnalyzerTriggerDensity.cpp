// Copyright Epic Games, Inc. All Rights Reserved.

#include "Analysis/MetasoundFrontendVertexAnalyzerTriggerDensity.h"

#include "MetasoundDataReference.h"
#include "MetasoundPrimitives.h"
#include "MetasoundRouter.h"
#include "MetasoundTrigger.h"


namespace Metasound
{
	namespace Frontend
	{
		const FAnalyzerOutput FVertexAnalyzerTriggerDensity::FOutputs::Value = { "TriggerDensity", GetMetasoundDataTypeName<float>() };

		FVertexAnalyzerTriggerDensity::FVertexAnalyzerTriggerDensity(const FCreateAnalyzerParams& InParams)
			: TVertexAnalyzer(InParams)
			, NumFramesPerBlock(InParams.OperatorSettings.GetNumFramesPerBlock())
		{
			Audio::FEnvelopeFollowerInitParams Params;
			Params.Mode = Audio::EPeakMode::Peak;
			Params.SampleRate = OperatorSettings.GetSampleRate();
			Params.NumChannels = 1;
			Params.AttackTimeMsec = 0;
			Params.ReleaseTimeMsec = 120;
			EnvelopeFollower.Init(Params);

			FAnalyzerAddress OutputAddress = InParams.AnalyzerAddress;
			OutputAddress.DataType = FOutputs::Value.DataType;
			OutputAddress.AnalyzerMemberName = FOutputs::Value.Name;
			FSendAddress SendAddress = OutputAddress.ToSendAddress();

			const FSenderInitParams InitParams { InParams.OperatorSettings, 0.0f };
			Sender = FDataTransmissionCenter::Get().RegisterNewSender(MoveTemp(SendAddress), InitParams);
		}

		void FVertexAnalyzerTriggerDensity::Execute()
		{
			const FTrigger& Trigger = GetAnalysisData();

			ScratchBuffer.Reset();
			ScratchBuffer.AddZeroed(NumFramesPerBlock);
			for (int32 i = 0; i < Trigger.Num(); ++i)
			{
				const int32 TriggerIndex = Trigger[i];
				// Can trigger in the future so ignore those beyond the current block
				if (TriggerIndex < ScratchBuffer.Num())
				{
					ScratchBuffer[TriggerIndex] = 1.0f;
				}
			}

			EnvelopeFollower.ProcessAudio(ScratchBuffer.GetData(), ScratchBuffer.Num());

			check(EnvelopeFollower.GetEnvelopeValues().Num() == 1);
			const float EnvelopeValue = EnvelopeFollower.GetEnvelopeValues().Last();
			FLiteral Literal;
			Literal.Set(EnvelopeValue);
			Sender->PushLiteral(Literal);
		}
	} // namespace Frontend
} // namespace Metasound
