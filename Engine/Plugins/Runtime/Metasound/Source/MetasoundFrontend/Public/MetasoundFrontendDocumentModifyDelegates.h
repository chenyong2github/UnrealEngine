// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Delegates/DelegateCombinations.h"
#include "MetasoundFrontendDocument.h"
#include "Templates/SharedPointer.h"


DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetaSoundFrontendDocumentMutateArray, int32 /* Index */);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetaSoundFrontendDocumentMutateInterfaceArray, const FMetasoundFrontendInterface& /* Interface */);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray, int32 /* NodeIndex */, int32 /* VertexIndex */, int32 /* LiteralIndex */);


namespace Metasound::Frontend
{
	struct METASOUNDFRONTEND_API FInterfaceModifyDelegates
	{
		FOnMetaSoundFrontendDocumentMutateInterfaceArray OnInterfaceAdded;
		FOnMetaSoundFrontendDocumentMutateInterfaceArray OnRemovingInterface;

		FOnMetaSoundFrontendDocumentMutateArray OnInputAdded;
		FOnMetaSoundFrontendDocumentMutateArray OnRemovingInput;

		FOnMetaSoundFrontendDocumentMutateArray OnOutputAdded;
		FOnMetaSoundFrontendDocumentMutateArray OnRemovingOutput;
	};

	struct METASOUNDFRONTEND_API FNodeModifyDelegates
	{
		FOnMetaSoundFrontendDocumentMutateArray OnNodeAdded;
		FOnMetaSoundFrontendDocumentMutateArray OnRemovingNode;

		FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray OnNodeInputLiteralSet;
		FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray OnRemovingNodeInputLiteral;
	};

	struct METASOUNDFRONTEND_API FEdgeModifyDelegates
	{
		FOnMetaSoundFrontendDocumentMutateArray OnEdgeAdded;
		FOnMetaSoundFrontendDocumentMutateArray OnRemovingEdge;
	};

	struct METASOUNDFRONTEND_API FDocumentModifyDelegates : TSharedFromThis<FDocumentModifyDelegates>
	{
		FOnMetaSoundFrontendDocumentMutateArray OnDependencyAdded;
		FOnMetaSoundFrontendDocumentMutateArray OnRemovingDependency;

		FInterfaceModifyDelegates InterfaceDelegates;
		FNodeModifyDelegates NodeDelegates;
		FEdgeModifyDelegates EdgeDelegates;
	};
} // namespace Metasound::Frontend
