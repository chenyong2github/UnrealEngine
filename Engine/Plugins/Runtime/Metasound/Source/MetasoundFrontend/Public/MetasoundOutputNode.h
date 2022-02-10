// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuildError.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundNode.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundGraphCore"

namespace Metasound
{
	/** FMissingOutputNodeInputReferenceError
	 *
	 * Caused by Output not being able to generate an IOperator instance due to
	 * the type requiring an input reference (i.e. it is not default constructable).
	 */
	class FMissingOutputNodeInputReferenceError : public FBuildErrorBase
	{
	public:
		FMissingOutputNodeInputReferenceError(const INode& InNode, const FText& InDataType)
			: FBuildErrorBase(
				"MetasoundMissingOutputDataReferenceError",
				METASOUND_LOCTEXT_FORMAT("MissingOutputNodeInputReferenceError", "Missing required output node input reference for type {0}.", InDataType))
		{
			AddNode(InNode);
		}

		virtual ~FMissingOutputNodeInputReferenceError() = default;

	};

	template<typename DataType>
	class TOutputNode : public FNode
	{
		class FOutputOperator : public IOperator
		{
			public:
				using FDataReadReference = TDataReadReference<DataType>;

				FOutputOperator(const FVertexName& InDataReferenceName, FDataReadReference InDataReference)
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
				FOutputOperatorFactory(const FVertexName& InDataReferenceName)
				:	DataReferenceName(InDataReferenceName)
				{
				}

				virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
				{
					const bool bContainsRef = InParams.InputDataReferences.ContainsDataReadReference<DataType>(DataReferenceName);
					if (bContainsRef)
					{
						TDataReadReference<DataType> InputReadRef = InParams.InputDataReferences.GetDataReadReference<DataType>(DataReferenceName);
						return MakeUnique<FOutputOperator>(DataReferenceName, InputReadRef);
					}

					// Only construct default if default construction is supported
					if constexpr (TIsParsable<DataType>::Value)
					{
						TDataReadReference<DataType> DefaultReadRef = TDataReadReferenceFactory<DataType>::CreateAny(InParams.OperatorSettings);
						return MakeUnique<FOutputOperator>(DataReferenceName, DefaultReadRef);
					}

					OutErrors.Emplace(MakeUnique<FMissingOutputNodeInputReferenceError>(InParams.Node, GetMetasoundDataTypeDisplayText<DataType>()));
					return TUniquePtr<IOperator>(nullptr);
				}

			private:
				FVertexName DataReferenceName;
		};

		static FVertexInterface GetVertexInterface(const FVertexName& InVertexName)
		{
			static const FText VertexDescription = METASOUND_LOCTEXT("Metasound_OutputVertexDescription", "Output from the parent Metasound graph.");

			return FVertexInterface(
				FInputVertexInterface(
					TInputDataVertexModel<DataType>(InVertexName, VertexDescription)
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<DataType>(InVertexName, VertexDescription)
				)
			);
		}

		static FNodeClassMetadata GetNodeInfo(const FVertexName& InVertexName)
		{
			FNodeClassMetadata Info;

			Info.ClassName = { "Output", GetMetasoundDataTypeName<DataType>(), FName() };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.Description = METASOUND_LOCTEXT("Metasound_OutputNodeDescription", "Output from the parent Metasound graph.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface(InVertexName);

			return Info;
		};



		public:
			TOutputNode(const FVertexName& InInstanceName, const FGuid& InInstanceID, const FVertexName& InVertexName)
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
