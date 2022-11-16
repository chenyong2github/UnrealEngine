// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Dataflow/DataflowSelection.h"

#include "GeometryCollectionSelectionNodes.generated.h"


class FGeometryCollection;


/**
 *
 * Selects all the bones for the Collection
 *
 */
USTRUCT()
struct FCollectionTransformSelectionAllDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionAllDataflowNode, "CollectionTransformSelectAll", "GeometryCollection|Selection", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionAllDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class ESetOperationEnum : uint8
{
	Dataflow_SetOperation_AND UMETA(DisplayName = "AND"),
	Dataflow_SetOperation_OR UMETA(DisplayName = "OR"),
	Dataflow_SetOperation_XOR UMETA(DisplayName = "XOR"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Runs boolean operation on incoming TransformSelections
 *
 */
USTRUCT()
struct FCollectionTransformSelectionSetOperationDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionSetOperationDataflowNode, "CollectionTransformSelectionSetOperation", "GeometryCollection|Selection", "")

public:
	/** Boolean operation */
	UPROPERTY(EditAnywhere, Category = "Compare");
	ESetOperationEnum Operation = ESetOperationEnum::Dataflow_SetOperation_AND;

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelectionA"))
	FDataflowTransformSelection TransformSelectionA;

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelectionB"))
	FDataflowTransformSelection TransformSelectionB;

	/** Array of the selected bone indicies after operation*/
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionSetOperationDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelectionA);
		RegisterInputConnection(&TransformSelectionB);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Generates a formatted string of the bones and the selection
 *
 */
USTRUCT()
struct FCollectionTransformSelectionInfoDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionInfoDataflowNode, "CollectionTransformSelectionInfo", "GeometryCollection|Selection", "")

public:
	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput))
	FManagedArrayCollection Collection;

	/** Formatted string of the bones and selection */
	UPROPERTY(meta = (DataflowOutput))
	FString String;

	FCollectionTransformSelectionInfoDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&String);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Generates an empty bone selection for the Collection
 *
 */
USTRUCT()
struct FCollectionTransformSelectionNoneDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionNoneDataflowNode, "CollectionTransformSelectNone", "GeometryCollection|Selection", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionNoneDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Inverts the incoming selection of bones
 *
 */
USTRUCT()
struct FCollectionTransformSelectionInvertDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionInvertDataflowNode, "CollectionTransformSelectInvert", "GeometryCollection|Selection", "")

public:
	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionInvertDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects bones randomly in the Collection
 *
 */
USTRUCT()
struct FCollectionTransformSelectionRandomDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionRandomDataflowNode, "CollectionTransformSelectRandom", "GeometryCollection|Selection", "")

public:
	/** If true, it always generates the same result for the same RandomSeed */
	UPROPERTY(EditAnywhere, Category = "Random")
	bool Deterministic = false;

	/** Seed for the random generation, only used if Deterministic is on */
	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput, EditCondition = "Deterministic"))
	float RandomSeed = 0.f;

	/** Bones get selected if RandomValue > RandomThreshold */
	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput, UIMin = 0.f, UIMax = 1.f))
	float RandomThreshold = 0.5f;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionRandomDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RandomSeed = FMath::FRandRange(-1e5, 1e5);
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&RandomThreshold);
		RegisterOutputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the root bones in the Collection
 *
 */
USTRUCT()
struct FCollectionTransformSelectionRootDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionRootDataflowNode, "CollectionTransformSelectRoot", "GeometryCollection|Selection", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionRootDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 * 
 * Selects specified bones in the GeometryCollection by using a 
 * space separated list
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionCustomDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionCustomDataflowNode, "CollectionTransformSelectCustom", "GeometryCollection|Selection", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Space separated list of bone indicies to specify the selection */
	UPROPERTY(EditAnywhere, Category = "Selection")
	FString BoneIndicies = FString();

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionCustomDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&BoneIndicies);
		RegisterOutputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the parents of the currently selected bones
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionParentDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionParentDataflowNode, "CollectionTransformSelectParent", "GeometryCollection|Selection", "")

public:
	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FCollectionTransformSelectionParentDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Outputs the specified percentage of the incoming bone selection
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionByPercentageDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionByPercentageDataflowNode, "CollectionTransformSelectByPercentage", "GeometryCollection|Selection", "")

public:
	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Percentage to keep from the original selection */
	UPROPERTY(EditAnywhere, Category = "Selection", meta = (UIMin = 0, UIMax = 100))
	int32 Percentage = 100;

	/** Sets the random generation to deterministic */
	UPROPERTY(EditAnywhere, Category = "Random")
	bool Deterministic = false;

	/** Seed value for the random generation */
	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput, EditCondition = "Deterministic"))
	float RandomSeed = 0.f;

	FCollectionTransformSelectionByPercentageDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RandomSeed = FMath::RandRange(-100000, 100000);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&Percentage);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the children of the incoming bone selection
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionChildrenDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionChildrenDataflowNode, "CollectionTransformSelectChildren", "GeometryCollection|Selection", "")

public:
	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FCollectionTransformSelectionChildrenDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the siblings of the incoming bone selection
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionSiblingsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionSiblingsDataflowNode, "CollectionTransformSelectSiblings", "GeometryCollection|Selection", "")

public:
	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FCollectionTransformSelectionSiblingsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the level of the incoming bone selection
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionLevelDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionLevelDataflowNode, "CollectionTransformSelectLevel", "GeometryCollection|Selection", "")

public:
	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FCollectionTransformSelectionLevelDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the contact(s) of the incoming bone selection
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionContactDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionContactDataflowNode, "CollectionTransformSelectContact", "GeometryCollection|Selection", "")

public:
	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FCollectionTransformSelectionContactDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the leaves in the GeometryCollection
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionLeafDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionLeafDataflowNode, "CollectionTransformSelectLeaf", "GeometryCollection|Selection", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionLeafDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the clusters in the GeometryCollection
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionClusterDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionClusterDataflowNode, "CollectionTransformSelectCluster", "GeometryCollection|Selection", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionClusterDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects pieces based on their size
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionBySizeDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionBySizeDataflowNode, "CollectionTransformSelectBySize", "GeometryCollection|Selection", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Minimum size for the selection */
	UPROPERTY(EditAnywhere, Category = "Size", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float SizeMin = 0.f;

	/** Maximum size for the selection */
	UPROPERTY(EditAnywhere, Category = "Size", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float SizeMax = 1000.f;

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionBySizeDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&SizeMin);
		RegisterInputConnection(&SizeMax);
		RegisterOutputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects pieces based on their volume
 *
 */
USTRUCT(meta = (DataflowDeprecated, DataflowTerminal))
struct FCollectionTransformSelectionByVolumeDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionByVolumeDataflowNode, "CollectionTransformSelectByVolume", "GeometryCollection|Selection", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Minimum volume for the selection */
	UPROPERTY(EditAnywhere, Category = "Volume", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float VolumeMin = 0.f;

	/** Maximum volume for the selection */
	UPROPERTY(EditAnywhere, Category = "Volume", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float VolumeMax = 1000.f;

	/** Array of the selected bone indicies */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionByVolumeDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&VolumeMin);
		RegisterInputConnection(&VolumeMax);
		RegisterOutputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


namespace Dataflow
{
	void GeometryCollectionSelectionNodes();
}

