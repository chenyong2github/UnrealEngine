// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Analysis/MetasoundFrontendAnalyzerFactory.h"
#include "Containers/Array.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/EnvelopeFollower.h"
#include "MetasoundTrigger.h"


namespace Metasound
{
	namespace Frontend
	{
		class METASOUNDFRONTEND_API FVertexAnalyzerTriggerDensity : public TVertexAnalyzer<FVertexAnalyzerTriggerDensity, FTrigger>
		{
			TUniquePtr<ISender> Sender;
			int32 NumFramesPerBlock = 0;

			Audio::FEnvelopeFollower EnvelopeFollower;
			Audio::FAlignedFloatBuffer ScratchBuffer;

		public:
			static const FName& GetAnalyzerName()
			{
				static const FName AnalyzerName = "UE.Trigger.Density";
				return AnalyzerName;
			}

			struct METASOUNDFRONTEND_API FOutputs
			{
				static const FAnalyzerOutput Value;

			};

			class METASOUNDFRONTEND_API FFactory : public TVertexAnalyzerFactory<FVertexAnalyzerTriggerDensity>
			{
			public:
				virtual const TArray<FAnalyzerOutput>& GetAnalyzerOutputs() const override
				{
					static const TArray<FAnalyzerOutput> Outputs { FOutputs::Value };
					return Outputs;
				}
			};

			FVertexAnalyzerTriggerDensity(const FCreateAnalyzerParams& InParams);
			virtual ~FVertexAnalyzerTriggerDensity() = default;

			virtual void Execute() override;
		};
	} // namespace Frontend
} // namespace Metasound
