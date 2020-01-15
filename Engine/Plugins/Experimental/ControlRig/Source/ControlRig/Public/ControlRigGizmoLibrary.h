// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

#include "ControlRigGizmoLibrary.generated.h"

USTRUCT(BlueprintType, meta = (DisplayName = "Gizmo"))
struct CONTROLRIG_API FControlRigGizmoDefinition
{
	GENERATED_USTRUCT_BODY()

	FControlRigGizmoDefinition()
	{
		GizmoName = TEXT("Gizmo");
		StaticMesh = nullptr;
		Transform = FTransform::Identity;
	}

	UPROPERTY(EditAnywhere, Category = "Gizmo")
	FName GizmoName;

	UPROPERTY(EditAnywhere, Category = "Gizmo")
	TAssetPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, Category = "Gizmo")
	FTransform Transform;
};

UCLASS(BlueprintType, meta = (DisplayName = "GizmoLibrary"))
class CONTROLRIG_API UControlRigGizmoLibrary : public UObject
{
	GENERATED_BODY()

public:

	UControlRigGizmoLibrary();

	UPROPERTY(EditAnywhere, Category = "GizmoLibrary")
	FControlRigGizmoDefinition DefaultGizmo;

	UPROPERTY(EditAnywhere, Category = "GizmoLibrary")
	TAssetPtr<UMaterial> DefaultMaterial;

	UPROPERTY(EditAnywhere, Category = "GizmoLibrary")
	FName MaterialColorParameter;

	UPROPERTY(EditAnywhere, Category = "GizmoLibrary")
	TArray<FControlRigGizmoDefinition> Gizmos;

	const FControlRigGizmoDefinition* GetGizmoByName(const FName& InName, bool bUseDefaultIfNotFound = false) const;

#if WITH_EDITOR

	// UObject interface
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

#endif

private:

	const TArray<FName> GetUpdatedNameList(bool bReset = false);

	TArray<FName> NameList;
};