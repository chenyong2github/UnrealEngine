// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"

namespace Metasound
{
	class METASOUNDGRAPHCORE_API FOscNode : public FNode
	{
			class FOperatorFactory : public IOperatorFactory
			{
				virtual TUniquePtr<IOperator> CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) override;
			};

		public:
			static const FName ClassName;

			FOscNode(const FString& InName, float InDefaultFrequency);

			virtual ~FOscNode();

			float GetDefaultFrequency() const;

			virtual const FName& GetClassName() const override;

			virtual IOperatorFactory& GetDefaultOperatorFactory() override;

		private:

			float DefaultFrequency;
			FOperatorFactory Factory;
	};
}
