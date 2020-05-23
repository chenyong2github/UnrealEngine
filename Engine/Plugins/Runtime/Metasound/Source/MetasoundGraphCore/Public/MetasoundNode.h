// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	//DECLARE_MULTICAST_DELEGATE_TwoParams(FParameterChangedDelegate, const INodeBase*, const FDataVertexDescription&);

	class METASOUNDGRAPHCORE_API FNode : public INode
	{
		public:
			FNode(const FString& InDescription);
			virtual ~FNode();

			virtual const FString& GetDescription() const override;

			virtual const TArray<FInputDataVertexDescription>& GetInputs() const override;
			virtual const TArray<FOutputDataVertexDescription>& GetOutputs() const override;

		protected:


			template<typename DataType>
			void AddInputDataVertexDescription(const FString& InVertexName, const FText& InVertexTooltip)
			{
				AddInputDataVertexDescription(MakeInputDataVertexDescription<DataType>(InVertexName, InVertexTooltip));
			}

			void AddInputDataVertexDescription(const FInputDataVertexDescription& InDescription);
			void RemoveInputDataVertexDescription(const FInputDataVertexDescription& InDescription);

			template<typename DataType>
			void AddOutputDataVertexDescription(const FString& InVertexName, const FText& InVertexTooltip)
			{
				AddOutputDataVertexDescription(MakeOutputDataVertexDescription<DataType>(InVertexName, InVertexTooltip));
			}

			void AddOutputDataVertexDescription(const FOutputDataVertexDescription& InDescription);
			void RemoveOutputDataVertexDescription(const FOutputDataVertexDescription& InDescription);

		private:

			FString Description;

			TArray<FInputDataVertexDescription> Inputs;
			TArray<FOutputDataVertexDescription> Outputs;
	};
}
