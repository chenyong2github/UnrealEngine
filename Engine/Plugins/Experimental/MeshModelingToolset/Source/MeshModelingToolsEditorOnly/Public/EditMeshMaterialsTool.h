// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshSelectionTool.h"
#include "EditMeshMaterialsTool.generated.h"


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UEditMeshMaterialsToolBuilder : public UMeshSelectionToolBuilder
{
	GENERATED_BODY()

public:
	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};






UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UEditMeshMaterialsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = Materials, meta = (ArrayClamp = "Materials"))
	int SelectedMaterial = 0;

	UPROPERTY(EditAnywhere, Category=Materials)
	TArray<UMaterialInterface*> Materials;
};





UENUM()
enum class EEditMeshMaterialsToolActions
{
	NoAction,
	AssignMaterial
};



UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UEditMeshMaterialsEditActions : public UMeshSelectionToolActionPropertySet
{
	GENERATED_BODY()

public:
	UFUNCTION(CallInEditor, Category = MaterialEdits, meta = (DisplayName = "Assign Selected Material", DisplayPriority = 1))
	void AssignSelectedMaterial()
	{
		PostMaterialAction(EEditMeshMaterialsToolActions::AssignMaterial);
	}

	void PostMaterialAction(EEditMeshMaterialsToolActions Action);
};






/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UEditMeshMaterialsTool : public UMeshSelectionTool
{
	GENERATED_BODY()

public:
	virtual void Setup() override;
	virtual void OnTick(float DeltaTime) override;

	virtual bool CanAccept() const override { return UMeshSelectionTool::CanAccept() || bHaveModifiedMaterials; }

	virtual void OnShutdown(EToolShutdownType ShutdownType) override;

	void RequestMaterialAction(EEditMeshMaterialsToolActions ActionType);

protected:
	UPROPERTY()
	UEditMeshMaterialsToolProperties* MaterialProps;

	virtual UMeshSelectionToolActionPropertySet* CreateEditActions() override;
	virtual void AddSubclassPropertySets() override;

	bool bHavePendingSubAction = false;
	EEditMeshMaterialsToolActions PendingSubAction = EEditMeshMaterialsToolActions::NoAction;

	void ApplyMaterialAction(EEditMeshMaterialsToolActions ActionType);
	void AssignMaterialToSelectedTriangles();

	TArray<UMaterialInterface*> CurrentMaterials;
	void OnMaterialSetChanged();

	struct FMaterialSetKey
	{
		TArray<void*> Values;
		bool operator!=(const FMaterialSetKey& Key2) const;
	};
	FMaterialSetKey GetMaterialKey();

	FMaterialSetKey InitialMaterialKey;
	bool bHaveModifiedMaterials = false;

	void ExternalUpdateMaterialSet(const TArray<UMaterialInterface*>& NewMaterialSet);
	friend class FEditMeshMaterials_MaterialSetChange;
};




/**
 */
class MESHMODELINGTOOLSEDITORONLY_API FEditMeshMaterials_MaterialSetChange : public FToolCommandChange
{
public:
	TArray<UMaterialInterface*> MaterialsBefore;
	TArray<UMaterialInterface*> MaterialsAfter;

	/** Makes the change to the object */
	virtual void Apply(UObject* Object) override;

	/** Reverts change to the object */
	virtual void Revert(UObject* Object) override;

	/** Describes this change (for debugging) */
	virtual FString ToString() const override;
};

