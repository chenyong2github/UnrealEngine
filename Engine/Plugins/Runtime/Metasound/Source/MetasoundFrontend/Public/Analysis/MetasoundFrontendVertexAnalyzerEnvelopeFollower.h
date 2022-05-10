// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Analysis/MetasoundFrontendAnalyzerFactory.h"
#include "Analysis/MetasoundFrontendVertexAnalyzer.h"
#include "Containers/Array.h"
#include "DSP/EnvelopeFollower.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundRouter.h"


namespace Metasound
{
	namespace Frontend
	{
		class METASOUNDFRONTEND_API FVertexAnalyzerEnvelopeFollower : public TVertexAnalyzer<FVertexAnalyzerEnvelopeFollower, FAudioBuffer>
		{
			TUniquePtr<ISender> Sender;
			Audio::FEnvelopeFollower EnvelopeFollower;

		public:
			static const FName& GetAnalyzerName()
			{
				static const FName AnalyzerName = "UE.Audio.EnvelopeFollower";
				return AnalyzerName;
			}

			struct METASOUNDFRONTEND_API FOutputs
			{
				static const FAnalyzerOutput Value;
			};

			class METASOUNDFRONTEND_API FFactory : public TVertexAnalyzerFactory<FVertexAnalyzerEnvelopeFollower>
			{
			public:
				virtual const TArray<FAnalyzerOutput>& GetAnalyzerOutputs() const override
				{
					static const TArray<FAnalyzerOutput> Outputs { FOutputs::Value };
					return Outputs;
				}
			};

			FVertexAnalyzerEnvelopeFollower(const FCreateAnalyzerParams& InParams);
			virtual ~FVertexAnalyzerEnvelopeFollower() = default;

			virtual void Execute() override;
		};
	} // namespace Frontend
} // namespace Metasound
