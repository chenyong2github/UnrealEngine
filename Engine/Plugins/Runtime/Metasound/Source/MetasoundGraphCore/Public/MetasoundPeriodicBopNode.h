// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundDataReferenceTypes.h"

namespace Metasound
{
	class METASOUNDGRAPHCORE_API FPeriodicBopNode : public FNode
	{

			class FOperatorFactory : public IOperatorFactory
			{
				virtual TUniquePtr<IOperator> CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) override;
			};

		public:
			static const FName ClassName;

			FPeriodicBopNode(const FString& InName, float InDefaultPeriodInSeconds);
			FPeriodicBopNode(const FNodeInitData& InInitData);

			virtual ~FPeriodicBopNode();

			float GetDefaultPeriodInSeconds() const;

			virtual const FName& GetClassName() const override;

			virtual IOperatorFactory& GetDefaultOperatorFactory() override;

			virtual const FString& GetDescription() const override
			{
				static FString StaticDescription = TEXT("This node emits a bop periodically, based on the duration given.");
				return StaticDescription;
			}

			virtual const FString& GetAuthorName() const override
			{
				static FString Author = TEXT("Epic Games");
				return Author;
			}

			virtual const FString& GetPromptIfMissing() const override
			{
				static FString Prompt = TEXT("Make sure that the Metasound plugin is loaded.");
				return Prompt;
			}


		private:
			float DefaultPeriod;
			FOperatorFactory Factory;
	};
}
