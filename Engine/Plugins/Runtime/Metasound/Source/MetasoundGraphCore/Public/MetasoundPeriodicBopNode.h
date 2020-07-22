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

			virtual const FText& GetDescription() const override
			{
				static const FText StaticDescription = NSLOCTEXT("MetasoundGraphCore", "Metasound_PeriodicBopNodeDescription", "Emits a bop periodically based on the period duration given.");
				return StaticDescription;
			}

			virtual const FText& GetAuthorName() const override
			{
				return PluginAuthor;
			}

			virtual const FText& GetPromptIfMissing() const override
			{
				return PluginNodeMissingPrompt;
			}

		private:
			float DefaultPeriod;
			FOperatorFactory Factory;
	};
}
