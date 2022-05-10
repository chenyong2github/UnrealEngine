// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Analysis/MetasoundFrontendAnalyzerAddress.h"
#include "HAL/Platform.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataReferenceCollection.h"
#include "Misc/Guid.h"


namespace Metasound
{
	namespace Frontend
	{
		struct METASOUNDFRONTEND_API FAnalyzerOutput
		{
			FName Name;
			FName DataType;
		};

		struct METASOUNDFRONTEND_API FCreateAnalyzerParams
		{
			const FAnalyzerAddress& AnalyzerAddress;
			const FOperatorSettings& OperatorSettings;
			const FDataReferenceCollection& Collection;
		};

		class METASOUNDFRONTEND_API IVertexAnalyzer
		{
		public:
			virtual ~IVertexAnalyzer() = default;

			virtual const FAnalyzerAddress& GetAnalyzerAddress() const = 0;
			virtual void Execute() = 0;
		};

		template<typename TChildClass, typename TDataType>
		class TVertexAnalyzer : public IVertexAnalyzer
		{
		protected:
			FAnalyzerAddress Address;
			FOperatorSettings OperatorSettings { 48000, 100 };
			TDataReadReference<TDataType> Reference;

		public:
			static FName GetDataType()
			{
				return GetMetasoundDataTypeName<TDataType>();
			}

			TVertexAnalyzer(const FCreateAnalyzerParams& InParams)
				: Address(InParams.AnalyzerAddress)
				, OperatorSettings(InParams.OperatorSettings)
				, Reference(InParams.Collection.GetDataReadReference<TDataType>(InParams.AnalyzerAddress.OutputName))
			{
				check(Address.AnalyzerName == TChildClass::GetAnalyzerName());
			}

			virtual ~TVertexAnalyzer() = default;

			const TDataType& GetAnalysisData() const
			{
				return *Reference;
			}

			virtual const FAnalyzerAddress& GetAnalyzerAddress() const override
			{
				return Address;
			}
		};
	} // namespace Frontend
} // namespace Metasound
