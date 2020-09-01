// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ControlRigDefines.h"
#include "Rigs/RigHierarchyContainer.h"
#include "ControlRigHierarchyModifier.generated.h"

UCLASS(BlueprintType)
class CONTROLRIGDEVELOPER_API UControlRigHierarchyModifier : public UObject
{
	GENERATED_BODY()

public:

	UControlRigHierarchyModifier();

#if WITH_EDITOR

	// Returns the keys of all elements within the hierarchy
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	TArray<FRigElementKey> GetElements() const;

	// Adds a new single bone
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FRigElementKey AddBone(
		const FName& InNewName,
		const FName& InParentName = NAME_None,
		ERigBoneType InType = ERigBoneType::User
	);

	// Returns a single bone from provided key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FRigBone GetBone(const FRigElementKey& InKey);

	// Updates a single bone
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetBone(const FRigBone& InElement);

	// Adds a new single control
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FRigElementKey AddControl(
		const FName& InNewName,
		ERigControlType InControlType = ERigControlType::Transform,
		const FName& InParentName = NAME_None,
		const FName& InSpaceName = NAME_None,
		const FName& InGizmoName = TEXT("Gizmo"),
		const FLinearColor& InGizmoColor = FLinearColor(1.0, 0.0, 0.0, 1.0)
	);

	// Returns a single control from provided key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FRigControl GetControl(const FRigElementKey& InKey);

	// Updates a single control
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetControl(const FRigControl& InElement);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	bool GetControlValueBool(const FRigElementKey& InKey, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	int32 GetControlValueInt(const FRigElementKey& InKey, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	float GetControlValueFloat(const FRigElementKey& InKey, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FVector2D GetControlValueVector2D(const FRigElementKey& InKey, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FVector GetControlValueVector(const FRigElementKey& InKey, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FRotator GetControlValueRotator(const FRigElementKey& InKey, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FTransform GetControlValueTransform(const FRigElementKey& InKey, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetControlValueBool(const FRigElementKey& InKey, bool InValue, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetControlValueInt(const FRigElementKey& InKey, int32 InValue, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetControlValueFloat(const FRigElementKey& InKey, float InValue, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetControlValueVector2D(const FRigElementKey& InKey, FVector2D InValue, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetControlValueVector(const FRigElementKey& InKey, FVector InValue, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetControlValueRotator(const FRigElementKey& InKey, FRotator InValue, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Sets a control value
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetControlValueTransform(const FRigElementKey& InKey, FTransform InValue, ERigControlValueType InValueType = ERigControlValueType::Initial);

	// Adds a new single space
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FRigElementKey AddSpace(
		const FName& InNewName,
		ERigSpaceType InSpaceType = ERigSpaceType::Global,
		const FName& InParentName = NAME_None
	);

	// Returns a single space from provided key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FRigSpace GetSpace(const FRigElementKey& InKey);

	// Updates a single space
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetSpace(const FRigSpace& InElement);

	// Adds a new single curve
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FRigElementKey AddCurve(const FName& InNewName, float InValue = 0.f);

	// Returns a single curve from provided key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FRigCurve GetCurve(const FRigElementKey& InKey);

	// Updates a single curve
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetCurve(const FRigCurve& InElement);

	// Removes a single element, returns true if successful
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	bool RemoveElement(const FRigElementKey& InElement);

	// Renames an existing element and returns the new element key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FRigElementKey RenameElement(const FRigElementKey& InElement, const FName& InNewName);

	// Reparents an element to another element, returns true if successful
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	bool ReparentElement(const FRigElementKey& InElement, const FRigElementKey& InNewParent);

	// Returns the keys of all selected elements within the hierarchy
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	TArray<FRigElementKey> GetSelection() const;

	// Selects or deselects a given element
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	bool Select(const FRigElementKey& InKey, bool bSelect = true);
	
	// Clears the selection
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	bool ClearSelection();

	// Returns true if a given element is currently selected
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	bool IsSelected(const FRigElementKey& InKey) const;

	// Initializes the rig, but calling reset on all elements
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void Initialize(bool bResetTransforms = true);
	
	// Removes all elements of the hierarchy
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void Reset();
	
	// Resets the transforms on all elements of the hierarchy
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void ResetTransforms();

	// Returns the initial transform for a given element key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FTransform GetInitialTransform(const FRigElementKey& InKey) const;

	// Sets the initial transform for a given element key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetInitialTransform(const FRigElementKey& InKey, const FTransform& InTransform);

	// Returns the initial global transform for a given element key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FTransform GetInitialGlobalTransform(const FRigElementKey& InKey) const;

	// Sets the initial global transform for a given element key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetInitialGlobalTransform(const FRigElementKey& InKey, const FTransform& InTransform);

	// Returns the current local transform of a given element key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FTransform GetLocalTransform(const FRigElementKey& InKey) const;

	// Sets the current local transform of a given element key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetLocalTransform(const FRigElementKey& InKey, const FTransform& InTransform);

	// Returns the current global transform of a given element key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FTransform GetGlobalTransform(const FRigElementKey& InKey) const;

	// Sets the current global transform of a given element key
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	void SetGlobalTransform(const FRigElementKey& InKey, const FTransform& InTransform);

	// Exports the elements provided to text (for copy & paste, import / export)
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	FString ExportToText(const TArray<FRigElementKey>& InElementsToExport) const;

	// Imports the content of the provided text and returns the keys created
	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	TArray<FRigElementKey> ImportFromText(const FString& InContent, ERigHierarchyImportMode InImportMode = ERigHierarchyImportMode::Append, bool bSelectNewElements = true);

#endif // WITH_EDITOR

private:

	FRigHierarchyContainer* Container;
	FRigBone InvalidBone;
	FRigControl InvalidControl;
	FRigSpace InvalidSpace;
	FRigCurve InvalidCurve;

	friend class UControlRigBlueprint;
};
