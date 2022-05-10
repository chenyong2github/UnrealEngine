// Copyright Epic Games, Inc. All Rights Reserved.

#include "Analysis/MetasoundFrontendVertexAnalyzerEnvelopeFollower.h"

#include "MetasoundDataReference.h"
#include "MetasoundPrimitives.h"
#include "MetasoundRouter.h"


namespace Metasound
{
	namespace Frontend
	{
		const FAnalyzerOutput FVertexAnalyzerEnvelopeFollower::FOutputs::Value = { "EnvelopeValue", GetMetasoundDataTypeName<float>() };

		FVertexAnalyzerEnvelopeFollower::FVertexAnalyzerEnvelopeFollower(const FCreateAnalyzerParams& InParams)
			: TVertexAnalyzer(InParams)
		{
			FAnalyzerAddress OutputAddress = InParams.AnalyzerAddress;
			OutputAddress.DataType = FOutputs::Value.DataType;
			OutputAddress.AnalyzerMemberName = FOutputs::Value.Name;
			FSendAddress SendAddress = OutputAddress.ToSendAddress();

			const FSenderInitParams InitParams { InParams.OperatorSettings, 0.0f };
			Sender = FDataTransmissionCenter::Get().RegisterNewSender(MoveTemp(SendAddress), InitParams);

			Audio::FEnvelopeFollowerInitParams Params;
			Params.Mode = Audio::EPeakMode::RootMeanSquared;
			Params.SampleRate = OperatorSettings.GetSampleRate();
			Params.NumChannels = 1;
			Params.AttackTimeMsec = 10;
			Params.ReleaseTimeMsec = 10;
			EnvelopeFollower.Init(Params);
		}

		void FVertexAnalyzerEnvelopeFollower::Execute()
		{
			const FAudioBuffer& InputData = GetAnalysisData();
			EnvelopeFollower.ProcessAudio(InputData.GetData(), InputData.Num());

			check(EnvelopeFollower.GetEnvelopeValues().Num() == 1);
			const float EnvelopeValue = EnvelopeFollower.GetEnvelopeValues().Last();

			FLiteral Literal;
			Literal.Set(EnvelopeValue);
			Sender->PushLiteral(Literal);
		}
	} // namespace Frontend
} // namespace Metasound
