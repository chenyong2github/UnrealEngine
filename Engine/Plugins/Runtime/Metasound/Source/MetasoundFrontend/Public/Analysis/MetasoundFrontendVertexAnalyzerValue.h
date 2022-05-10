// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Analysis/MetasoundFrontendAnalyzerAddress.h"
#include "Analysis/MetasoundFrontendAnalyzerFactory.h"
#include "Analysis/MetasoundFrontendVertexAnalyzer.h"
#include "Containers/Array.h"
#include "MetasoundDataReference.h"
#include "MetasoundPrimitives.h"
#include "MetasoundRouter.h"
#include "MetasoundTrigger.h"


namespace Metasound
{
	namespace Frontend
	{
		template <typename TChildClass, typename TDataType>
		class TVertexAnalyzerValue : public TVertexAnalyzer<TChildClass, TDataType>
		{
			TUniquePtr<ISender> Sender;
			TDataType LastValue;

		public:
			class FFactory : public TVertexAnalyzerFactory<TChildClass>
			{
			public:
				virtual const TArray<FAnalyzerOutput>& GetAnalyzerOutputs() const override
				{
					static const TArray<FAnalyzerOutput> Outputs { TChildClass::FOutputs::Value };
					return Outputs;
				}
			};

			TVertexAnalyzerValue(const FCreateAnalyzerParams& InParams)
 				: TVertexAnalyzer<TChildClass, TDataType>(InParams)
			{
				FAnalyzerAddress OutputAddress = InParams.AnalyzerAddress;
				OutputAddress.DataType = TChildClass::FOutputs::Value.DataType;
				OutputAddress.AnalyzerMemberName = TChildClass::FOutputs::Value.Name;
				FSendAddress SendAddress = OutputAddress.ToSendAddress();

				const FSenderInitParams InitParams{ InParams.OperatorSettings, 0.0f };
				Sender = FDataTransmissionCenter::Get().RegisterNewSender(MoveTemp(SendAddress), InitParams);
			}

			virtual ~TVertexAnalyzerValue() = default;

			virtual void Execute() override
			{
				const TDataType& Value = TVertexAnalyzer<TChildClass, TDataType>::GetAnalysisData();
				if (LastValue != Value)
				{
					FLiteral Literal;
					Literal.Set(Value);
					Sender->PushLiteral(Literal);
				}
			}
		};

		#define METASOUND_DECLARE_VALUE_ANALYZER(ClassType, AnalyzerFName, DataType) \
			class METASOUNDFRONTEND_API ClassType : public TVertexAnalyzerValue<ClassType, DataType> \
			{ \
			public: \
				static const FName& GetAnalyzerName() \
				{ \
					static const FName AnalyzerName = AnalyzerFName; return AnalyzerName; \
				} \
				struct METASOUNDFRONTEND_API FOutputs { static const FAnalyzerOutput Value; }; \
				ClassType(const FCreateAnalyzerParams& InParams) \
					: TVertexAnalyzerValue(InParams) { } \
				virtual ~ClassType() = default; \
			};

		#define METASOUND_DEFINE_VALUE_ANALYZER(ClassType, DataType) \
			const FAnalyzerOutput ClassType::FOutputs::Value = { "Value", GetMetasoundDataTypeName<DataType>() };

		METASOUND_DECLARE_VALUE_ANALYZER(FVertexAnalyzerBool, "UE.Bool", bool)
		METASOUND_DECLARE_VALUE_ANALYZER(FVertexAnalyzerFloat, "UE.Float", float)
		METASOUND_DECLARE_VALUE_ANALYZER(FVertexAnalyzerInt, "UE.Int32", int32)
		METASOUND_DECLARE_VALUE_ANALYZER(FVertexAnalyzerString, "UE.String", FString)
	} // namespace Frontend
} // namespace Metasound
