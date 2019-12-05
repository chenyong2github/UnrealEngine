// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BehaviorTree/BlackboardAssetProvider.h"


//----------------------------------------------------------------------//
// UBlackboardAssetProvider
//----------------------------------------------------------------------//

#if WITH_EDITOR
IBlackboardAssetProvider::FBlackboardOwnerChanged IBlackboardAssetProvider::OnBlackboardOwnerChanged;
#endif

UBlackboardAssetProvider::UBlackboardAssetProvider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}
