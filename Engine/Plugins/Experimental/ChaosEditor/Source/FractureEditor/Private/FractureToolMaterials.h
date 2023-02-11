// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolMaterials.generated.h"

class FFractureToolContext;
struct FMeshDescription;

UENUM()
enum class EMaterialAssignmentTargets
{
	OnlyInternalFaces,
	OnlyExternalFaces,
	AllFaces
};

/** Settings related to geometry collection -> static mesh conversion **/
UCLASS(config = EditorPerProjectUserSettings)
class UFractureMaterialsSettings : public UFractureToolSettings
{
public:

	GENERATED_BODY()

	UFractureMaterialsSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
	{}

	/** Add a new material slot to the selected geometry collections */
	UFUNCTION(CallInEditor, Category = MaterialEditing, meta = (DisplayName = "Add Material Slot"))
	void AddMaterialSlot();

	/** Remove the last material slot from the selected geometry collections. (Will not remove the final material.) */
	UFUNCTION(CallInEditor, Category = MaterialEditing, meta = (DisplayName = "Remove Material Slot"))
	void RemoveMaterialSlot();

	/** Material to assign to selected faces */
	UPROPERTY(EditAnywhere, Category = MaterialEditing, meta = (TransientToolProperty, DisplayName = "Assign Material", GetOptions = GetMaterialNamesFunc, NoResetToDefault))
	FString AssignMaterial;

	/** Which subset of faces to update materials assignments on, for the selected geometry */
	UPROPERTY(EditAnywhere, Category = MaterialEditing)
	EMaterialAssignmentTargets ToFaces = EMaterialAssignmentTargets::OnlyInternalFaces;

	/** Whether to only assign materials for faces in the selected bones, or the whole geometry collection */
	UPROPERTY(EditAnywhere, Category = MaterialEditing)
	bool bOnlySelected = true;

	UFUNCTION()
	const TArray<FString>& GetMaterialNamesFunc() { return AssignMaterialNamesList; }

	void UpdateActiveMaterialNames(TArray<FString> InMaterialNamesList)
	{
		AssignMaterialNamesList = InMaterialNamesList;
		if (!AssignMaterialNamesList.Contains(AssignMaterial))
		{
			AssignMaterial = AssignMaterialNamesList.IsEmpty() ? FString() : AssignMaterialNamesList[0];
		}
	}

	int32 GetAssignMaterialID()
	{
		return AssignMaterialNamesList.Find(AssignMaterial);
	}

private:

	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> AssignMaterialNamesList;

};


UCLASS(DisplayName = "Materials Tool", Category = "FractureTools")
class UFractureToolMaterials : public UFractureModalTool
{
public:
	GENERATED_BODY()

	UFractureToolMaterials(const FObjectInitializer& ObjInit);

	///
	/// UFractureModalTool Interface
	///

	/** This is the Text that will appear on the tool button to execute the tool **/
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	/** This is the Text that will appear on the button to execute the fracture **/
	virtual FText GetApplyText() const override { return FText(NSLOCTEXT("FractureToolMaterials", "ExecuteMaterials", "Assign Materials")); }
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual TArray<UObject*> GetSettingsObjects() const override;
	virtual void FractureContextChanged() override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	/** Executes function that generates new geometry. Returns the first new geometry index. */
	virtual int32 ExecuteFracture(const FFractureToolContext& FractureContext) override;
	virtual bool CanExecute() const override;
	virtual bool ExecuteUpdatesShape() const override
	{
		return false;
	}
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

	virtual void SelectedBonesChanged()
	{
		Super::SelectedBonesChanged();
		UpdateActiveMaterialsList();
	}

	// Called when the modal tool is entered
	virtual void Setup()
	{
		Super::Setup();
		UpdateActiveMaterialsList();
	}

	void UpdateActiveMaterialsList()
	{
		MaterialsSettings->UpdateActiveMaterialNames(GetSelectedComponentMaterialNames(false, false));
	}

	virtual TArray<FFractureToolContext> GetFractureToolContexts() const override;

	void RemoveMaterialSlot();
	void AddMaterialSlot();

protected:
	UPROPERTY(EditAnywhere, Category = Slicing)
	TObjectPtr<UFractureMaterialsSettings> MaterialsSettings;
};
