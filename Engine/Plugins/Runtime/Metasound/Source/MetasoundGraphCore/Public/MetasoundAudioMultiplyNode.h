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

			// Constructor used by the Metasound Frontend.
			FAudioMultiplyNode(const FNodeInitData& InInitData);

			virtual ~FAudioMultiplyNode();

			virtual const FName& GetClassName() const override;

			virtual IOperatorFactory& GetDefaultOperatorFactory() override;

			virtual const FString& GetDescription() const override
			{
				static FString StaticDescription = TEXT("This node multiplies to audio signals together. This is useful for amplitude modulation and other applications.");
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

			FOperatorFactory Factory;
	};

	
}
