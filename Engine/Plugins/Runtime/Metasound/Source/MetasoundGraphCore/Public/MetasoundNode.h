// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	class METASOUNDGRAPHCORE_API FNode : public INode
	{
		public:
			FNode(const FString& InDescription);
			virtual ~FNode();

			virtual const FString& GetDescription() const override;

			virtual const FInputDataVertexCollection& GetInputDataVertices() const override;
			virtual const FOutputDataVertexCollection& GetOutputDataVertices() const override;

		protected:


			template<typename DataType>
			void AddInputDataVertex(const FString& InVertexName, const FText& InVertexDescription)
			{
				AddInputDataVertex(MakeInputDataVertex<DataType>(InVertexName, InVertexDescription));
			}

			void AddInputDataVertex(const FInputDataVertex& InVertex);
			void RemoveInputDataVertex(const FInputDataVertex& InVertex);

			template<typename DataType>
			void AddOutputDataVertex(const FString& InVertexName, const FText& InVertexDescription)
			{
				AddOutputDataVertex(MakeOutputDataVertex<DataType>(InVertexName, InVertexDescription));
			}

			void AddOutputDataVertex(const FOutputDataVertex& InVertex);
			void RemoveOutputDataVertex(const FOutputDataVertex& InVertex);

		private:

			FString Description;

			FInputDataVertexCollection Inputs;
			FOutputDataVertexCollection Outputs;
	};
}
