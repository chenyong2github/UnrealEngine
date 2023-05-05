// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothSimulationProxy.h"

#include "ClothDataGenerationComponent.generated.h"

namespace UE::Chaos::ClothAsset
{
	class FClothSimulationDataGenerationProxy : public FClothSimulationProxy
    {
    public:
        explicit FClothSimulationDataGenerationProxy(const UChaosClothComponent& InClothComponent);
        ~FClothSimulationDataGenerationProxy();

        using FClothSimulationProxy::Tick;
        using FClothSimulationProxy::FillSimulationContext;
        using FClothSimulationProxy::InitializeConfigs;
        using FClothSimulationProxy::WriteSimulationData;
    };
};

/**
 * Cloth data generation component.
 */
UCLASS()
class UClothDataGenerationComponent : public UChaosClothComponent
{
	GENERATED_BODY()	
public:
	UClothDataGenerationComponent(const FObjectInitializer& ObjectInitializer);
	UClothDataGenerationComponent(FVTableHelper& Helper);
	~UClothDataGenerationComponent();

    using FDataProxy = UE::Chaos::ClothAsset::FClothSimulationDataGenerationProxy;
    TWeakPtr<FDataProxy> GetProxy() const;
    
	/** Pose the cloth component using component space transforms. */
    void Pose(const TArray<FTransform>& InComponentSpaceTransforms);
protected:
	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

    /** Begin UChaosClothComponent Interface */
	virtual TSharedPtr<UE::Chaos::ClothAsset::FClothSimulationProxy> CreateClothSimulationProxy() override;
    /** End UChaosClothComponent Interface */
private:
    TWeakPtr<FDataProxy> DataProxy;
};
