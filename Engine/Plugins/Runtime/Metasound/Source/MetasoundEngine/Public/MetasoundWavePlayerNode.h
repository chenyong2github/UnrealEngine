// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"

namespace Metasound
{
	class METASOUNDENGINE_API FWavePlayerNode : public FNode
	{
		class FOperatorFactory : public IOperatorFactory
		{
			virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override;
		};

		

	public:
		static FVertexInterface DeclareVertexInterface();
		static const FNodeInfo& GetNodeInfo();

		FWavePlayerNode(const FString& InName);

		// constructor used by the Metasound Frontend.
		FWavePlayerNode(const FNodeInitData& InInitData);

		virtual ~FWavePlayerNode() = default;

		virtual FOperatorFactorySharedRef GetDefaultOperatorFactory() const override;

		/** Return the current vertex interface. */
		virtual const FVertexInterface& GetVertexInterface() const override;

		/** Set the vertex interface. If the vertex was successfully changed, returns true. 
		 *
		 * @param InInterface - New interface for node. 
		 *
		 * @return True on success, false otherwise.
		 */
		virtual bool SetVertexInterface(const FVertexInterface& InInterface) override;

		/** Expresses whether a specific vertex interface is supported.
		 *
		 * @param InInterface - New interface. 
		 *
		 * @return True if the interface is supported, false otherwise. 
		 */
		virtual bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const override;

	private:

		FOperatorFactorySharedRef Factory;

		FVertexInterface Interface;
	};
}
