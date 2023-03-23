// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "SimulationXPBDConfigNode.generated.h"

class UChaosClothSharedSimConfig;
class UMaterialInterface;
namespace Chaos::Softs
{
class FCollectionPropertyMutableFacade;
}

USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetSimulationXPBDConfigNode : public FDataflowNode, public FGCObject
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetSimulationXPBDConfigNode, "SimulationXPBDConfig", "Cloth", "Cloth Simulation XPBD Config")
	//DATAFLOW_NODE_RENDER_TYPE(FManagedArrayCollection::StaticType(), "Collection")  // TODO: Leave out the render type until there is something to render

public:

	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/** Cloth Simulation Properties. */
	UPROPERTY(EditAnywhere, Category = "Stretch")
	bool bEnableXPBDStretchConstraints = true;

	UPROPERTY(EditAnywhere, Category = "Stretch", DisplayName = "Stretch Stiffness [kg/s^2]", meta = (UIMin = "0", UIMax = "10000", ClampMin = "0", ClampMax = "10000000", EditCondition = "bEnableXPBDStretchConstraints"))
	FChaosClothWeightedValue StretchStiffness = { 100.f, 100.f };
	UPROPERTY(EditAnywhere, Category = "Stretch", DisplayName = "Stretch Damping Ratio", meta = (UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "1000", EditCondition = "bEnableXPBDStretchConstraints"))
	FChaosClothWeightedValue StretchDampingRatio = { 1.f, 1.f };

	UPROPERTY(EditAnywhere, Category = "Bending")
	bool bEnableXPBDBendConstraints = true;
	UPROPERTY(EditAnywhere, Category = "Bending")
	bool bEnableBendAnisotropy = false;

	UPROPERTY(EditAnywhere, Category = "Bending", DisplayName = "Bending Stiffness (Warp) [kg cm/s^2 rad]", meta = (UIMin = "0", UIMax = "10000", ClampMin = "0", ClampMax = "10000000", EditCondition = "bEnableXPBDBendConstraints"))
	FChaosClothWeightedValue BendingStiffness = { 100.f, 100.f };

	UPROPERTY(EditAnywhere, Category = "Bending", DisplayName = "Bending Stiffness (Weft) [kg cm/s^2 rad]", meta = (UIMin = "0", UIMax = "10000", ClampMin = "0", ClampMax = "10000000", EditCondition = "bEnableXPBDBendConstraints && bEnableBendAnisotropy"))
	FChaosClothWeightedValue BendingStiffnessWeft = { 100.f, 100.f };

	UPROPERTY(EditAnywhere, Category = "Bending", DisplayName = "Bending Stiffness (Bias) [kg cm/s^2 rad]", meta = (UIMin = "0", UIMax = "10000", ClampMin = "0", ClampMax = "10000000", EditCondition = "bEnableXPBDBendConstraints && bEnableBendAnisotropy"))
	FChaosClothWeightedValue BendingStiffnessBias = { 100.f, 100.f };

	UPROPERTY(EditAnywhere, Category = "Bending", DisplayName = "Bending Damping Ratio", meta = (UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "1000", EditCondition = "bEnableXPBDBendConstraints"))
	FChaosClothWeightedValue BendingDampingRatio = { 1.f, 1.f };
		
	UPROPERTY(EditAnywhere, Category = "Bending", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "bEnableXPBDBendConstraints"))
	float BucklingRatio = 0.f;

	UPROPERTY(EditAnywhere, Category = "Bending", DisplayName = "Buckling Stiffness (Warp) [kg cm/s^2 rad]", meta = (UIMin = "0", UIMax = "10000", ClampMin = "0", ClampMax = "10000000", EditCondition = "bEnableXPBDBendConstraints && bEnableXPBDBendConstraints"))
	FChaosClothWeightedValue BucklingStiffness = { 100.f, 100.f };

	UPROPERTY(EditAnywhere, Category = "Bending", DisplayName = "Buckling Stiffness (Weft) [kg cm/s^2 rad]", meta = (UIMin = "0", UIMax = "10000", ClampMin = "0", ClampMax = "10000000", EditCondition = "bEnableXPBDBendConstraints && bEnableBendAnisotropy"))
	FChaosClothWeightedValue BucklingStiffnessWeft = { 100.f, 100.f };

	UPROPERTY(EditAnywhere, Category = "Bending", DisplayName = "Buckling Stiffness (Bias) [kg cm/s^2 rad]", meta = (UIMin = "0", UIMax = "10000", ClampMin = "0", ClampMax = "10000000", EditCondition = "bEnableXPBDBendConstraints && bEnableBendAnisotropy"))
	FChaosClothWeightedValue BucklingStiffnessBias = { 100.f, 100.f };

	UPROPERTY(EditAnywhere, Category = "Area")
	bool bEnableXPBDAreaConstraints = true;
	
	UPROPERTY(EditAnywhere, Category = "Area", DisplayName = "Area Stiffness [kg/s^2]", meta = (UIMin = "0", UIMax = "10000", ClampMin = "0", ClampMax = "10000000", EditCondition = "bEnableXPBDAreaConstraints"))
	FChaosClothWeightedValue AreaStiffness = { 100.f, 100.f };

	/** Cloth Shared Simulation Properties. */
	UPROPERTY(EditAnywhere, Instanced, NoClear, Category = "Dataflow")
	TObjectPtr<UChaosClothSharedSimConfig> SharedSimulationConfig;

	FChaosClothAssetSimulationXPBDConfigNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Serialize(FArchive& Ar) override;

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

	//~ Begin FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	//~ End FGCObject Interface

private:
	void SetStretchProperties(Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const;
	void SetBendingProperties(Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const;
	void SetAreaProperties(Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const;

};
