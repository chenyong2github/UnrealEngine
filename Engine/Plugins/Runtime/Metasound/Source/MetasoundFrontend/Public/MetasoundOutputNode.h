// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundNode.h"

#define LOCTEXT_NAMESPACE "MetasoundGraphCore"

namespace Metasound
{
	template<typename DataType>
	class TOutputNode : public FNode
	{
		class FOutputOperator : public IOperator
		{
			public:
				using FDataReadReference = TDataReadReference<DataType>;

				FOutputOperator(const FString& InDataReferenceName, FDataReadReference InDataReference)
				{
					Outputs.AddDataReadReference<DataType>(InDataReferenceName, InDataReference);
				}

				virtual ~FOutputOperator() {}

				virtual FDataReferenceCollection GetInputs() const override
				{
					return {};
				}

				virtual FDataReferenceCollection GetOutputs() const override
				{
					return Outputs;
				}

				virtual FExecuteFunction GetExecuteFunction() override
				{
					return nullptr;
				}

			private:
				FDataReferenceCollection Outputs;
		};

		class FOutputOperatorFactory : public IOperatorFactory
		{
			public:
				FOutputOperatorFactory(const FString& InDataReferenceName)
				:	DataReferenceName(InDataReferenceName)
				{
				}

				virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
				{
					if (!InParams.InputDataReferences.ContainsDataReadReference<DataType>(DataReferenceName))
					{
						// TODO: Add build error.
						return TUniquePtr<IOperator>(nullptr);
					}

					return MakeUnique<FOutputOperator>(DataReferenceName, InParams.InputDataReferences.GetDataReadReference<DataType>(DataReferenceName));
				}

			private:
				FString DataReferenceName;
		};

		static FVertexInterface GetVertexInterface(const FString& InVertexName)
		{
			return FVertexInterface(
				FInputVertexInterface(
					TInputDataVertexModel<DataType>(InVertexName, LOCTEXT("Metasound_OutputVertexDescription", "Output from the parent Metasound graph."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<DataType>(InVertexName, LOCTEXT("Metasound_OutputVertexDescription", "Output from the parent Metasound graph."))
				)
			);
		}

		static FNodeClassMetadata GetNodeInfo(const FString& InVertexName)
		{
			FNodeClassMetadata Info;

			Info.ClassName = {TEXT("Output"), GetMetasoundDataTypeName<DataType>(), TEXT("")};
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = FText::Format(LOCTEXT("Metasound_OutputNodeDisplayNameFormat", "Output {0}"), FText::FromName(GetMetasoundDataTypeName<DataType>()));
			Info.Description = LOCTEXT("Metasound_OutputNodeDescription", "Output from the parent Metasound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface(InVertexName);

			return Info;
		};



		public:
			TOutputNode(const FString& InInstanceName, const FGuid& InInstanceID, const FString& InVertexName)
			:	FNode(InInstanceName, InInstanceID, GetNodeInfo(InVertexName))
			,	VertexInterface(GetVertexInterface(InVertexName))
			,	Factory(MakeShared<FOutputOperatorFactory, ESPMode::ThreadSafe>(InVertexName))
			{
			}

			/** Return the current vertex interface. */
			virtual const FVertexInterface& GetVertexInterface() const override
			{
				return VertexInterface;
			}

			/** Set the vertex interface. If the vertex was successfully changed, returns true. 
			 *
			 * @param InInterface - New interface for node. 
			 *
			 * @return True on success, false otherwise.
			 */
			virtual bool SetVertexInterface(const FVertexInterface& InInterface) override
			{
				return VertexInterface == InInterface;
			}

			/** Expresses whether a specific vertex interface is supported.
			 *
			 * @param InInterface - New interface. 
			 *
			 * @return True if the interface is supported, false otherwise. 
			 */
			virtual bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const 
			{
				return VertexInterface == InInterface;
			}

			virtual TSharedRef<IOperatorFactory, ESPMode::ThreadSafe> GetDefaultOperatorFactory() const override
			{
				return Factory;
			}

		private:
			FVertexInterface VertexInterface;

			TSharedRef<FOutputOperatorFactory, ESPMode::ThreadSafe> Factory;

	};
}
#undef LOCTEXT_NAMESPACE // MetasoundOutputNode
