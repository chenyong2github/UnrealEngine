// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"

namespace Metasound
{
	/** FADSRNode
	 *
	 *  Creates an Attack, Decay Sustain, Release audio processer node. 
	 */
	class METASOUNDGRAPHCORE_API FADSRNode : public FNode
	{

			// The operator factory for this node.
			class FOperatorFactory : public IOperatorFactory
			{
				public:
					virtual TUniquePtr<IOperator> CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) override;

					template<typename DataType>
					void SetReadableRefIfInCollection(const FString& InName, const FDataReferenceCollection& InCollection, TDataReadReference<DataType>& DataRef)
					{
						// TODO: add a helper function to FDataReferenceCollection to do a SetDataIfContains.
						if (InCollection.ContainsDataReadReference<DataType>(InName))
						{
							DataRef = InCollection.GetDataReadReference<DataType>(InName);
						}
					}

			};

		public:
			/** Class name for FADSRNode */
			static const FName ClassName;

			/** FADSR node constructor.
			 *
			 * @param InName - Name of this node.
			 * @param InDefaultAttackMs - Default attack in milliseconds.
			 * @param InDefaultDecayMs - Default decay in milliseconds.
			 * @param InDefaultSustainMs - Default sustain in milliseconds.
			 * @param InDefaultReleaseMs - Default release in milliseconds.
			 */
			FADSRNode(const FString& InName, float InDefaultAttackMs, float InDefaultDecayMs, float InDefaultSustainMs, float InDefaultReleaseMs);

			/**
			 * Constructor used by the Metasound Frontend.
			 */
			FADSRNode(const FNodeInitData& InitData);

			virtual ~FADSRNode();

			/** Return default attack time in milliseconds */
			float GetDefaultAttackMs() const;
			/** Return default attack time in milliseconds */
			float GetDefaultDecayMs() const;
			/** Return default attack time in milliseconds */
			float GetDefaultSustainMs() const;
			/** Return default attack time in milliseconds */
			float GetDefaultReleaseMs() const;

			/** Returns the type name of the FADSRNode */
			virtual const FName& GetClassName() const override;

			virtual const FString& GetDescription() const override
			{
				static FString StaticDescription = TEXT("This node, when bopped, emits an envelope that can be applied to .");
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

			/** Return a factory for building a metasound operator. */
			virtual IOperatorFactory& GetDefaultOperatorFactory() override;

		private:

			float DefaultAttackMs;
			float DefaultDecayMs;
			float DefaultSustainMs;
			float DefaultReleaseMs;

			FOperatorFactory Factory;
	};


}
