// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothDataGenerationComponent.h"

namespace UE::Chaos::ClothAsset
{
    FClothSimulationDataGenerationProxy::FClothSimulationDataGenerationProxy(const UChaosClothComponent& InClothComponent)
	    : FClothSimulationProxy(InClothComponent)
    {	
    }

    FClothSimulationDataGenerationProxy::~FClothSimulationDataGenerationProxy() = default;
};

UClothDataGenerationComponent::UClothDataGenerationComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

UClothDataGenerationComponent::UClothDataGenerationComponent(FVTableHelper& Helper)
    : Super(Helper)
{
}

UClothDataGenerationComponent::~UClothDataGenerationComponent() = default;

TSharedPtr<UE::Chaos::ClothAsset::FClothSimulationProxy> UClothDataGenerationComponent::CreateClothSimulationProxy()
{
    TSharedPtr<FDataProxy> DataProxyShared = MakeShared<FDataProxy>(*this);
    DataProxy = DataProxyShared;
    return DataProxyShared;
}

void UClothDataGenerationComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
}


TWeakPtr<UE::Chaos::ClothAsset::FClothSimulationDataGenerationProxy> UClothDataGenerationComponent::GetProxy() const
{
    return DataProxy;
}

void UClothDataGenerationComponent::Pose(const TArray<FTransform>& InComponentSpaceTransforms)
{
	if (!ensure(InComponentSpaceTransforms.Num() == GetComponentSpaceTransforms().Num()))
	{
		return;
	}
	GetEditableComponentSpaceTransforms() = InComponentSpaceTransforms;
	bNeedToFlipSpaceBaseBuffers = true;
	FinalizeBoneTransform();

	UpdateBounds();
    if (IsInGameThread())
    {
        MarkRenderTransformDirty();
        MarkRenderDynamicDataDirty();
    }
}
