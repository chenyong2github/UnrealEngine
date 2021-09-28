// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeDelegates.h"
#include "CoreMinimal.h"

namespace UE::StateTree::Delegates
{
	
#if WITH_EDITOR
FOnIdentifierChanged OnIdentifierChanged;
FOnParametersInvalidated OnParametersInvalidated;
FOnParameterLayoutChanged OnParameterLayoutChanged;
FOnSchemaChanged OnSchemaChanged;
#endif
	
}; // UE::StateTree::Delegates
