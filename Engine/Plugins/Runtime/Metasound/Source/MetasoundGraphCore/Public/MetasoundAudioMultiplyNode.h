// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"

namespace Metasound
{
	class METASOUNDGRAPHCORE_API FAudioMultiplyNode : public FNode
	{
			class FOperatorFactory : public IOperatorFactory
			{
				public:
					virtual TUniquePtr<IOperator> CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) override;

					template<typename ParamType>
					bool SetReadableRefIfInCollection(const FString& InParamName, const FDataReferenceCollection& InCollection, TDataReadReference<ParamType>& ParamRef)
					{
						// TODO: add a helper function to FDataReferenceCollection to do a SetParamIfContains.
						if (InCollection.ContainsDataReadReference<ParamType>(InParamName))
						{
							ParamRef = InCollection.GetDataReadReference<ParamType>(InParamName);
							return true;
						}

						return false;
					}

			};

		public:
			static const FName ClassName;

			FAudioMultiplyNode(const FString& InName);

			virtual ~FAudioMultiplyNode();

			virtual const FName& GetClassName() const override;

			virtual IOperatorFactory& GetDefaultOperatorFactory() override;

		private:

			FOperatorFactory Factory;
	};
}
