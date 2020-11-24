// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFacade.h"

namespace Metasound
{
	FNodeFacade::FFactory::FFactory(FCreateOperatorFunction InCreateFunc)
	:	CreateFunc(InCreateFunc)
	{
	}

	TUniquePtr<IOperator> FNodeFacade::FFactory::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		return CreateFunc(InParams, OutErrors);
	}

	const FVertexInterface& FNodeFacade::GetVertexInterface() const
	{
		return VertexInterface;
	}

	bool FNodeFacade::SetVertexInterface(const FVertexInterface& InInterface)
	{
		return InInterface == VertexInterface;
	}

	bool FNodeFacade::IsVertexInterfaceSupported(const FVertexInterface& InInterface) const
	{
		return InInterface == VertexInterface;
	}

	/** Return a reference to the default operator factory. */
	FOperatorFactorySharedRef FNodeFacade::GetDefaultOperatorFactory() const
	{
		return Factory;
	}
}
