// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

#include "PersonaToolMenuContext.generated.h"

class IPersonaToolkit;
class USkeleton;
class UDebugSkelMeshComponent;
class USkeletalMesh;
class UAnimBlueprint;
class UAnimationAsset;

UCLASS(BlueprintType)
class PERSONA_API UPersonaToolMenuContext : public UObject
{
	GENERATED_BODY()
public:
	
	/** Get the skeleton that we are editing */
	UFUNCTION(BlueprintCallable, Category = PersonaEditorExtensions)
	USkeleton* GetSkeleton() const;

	/** Get the preview component that we are using */
	UFUNCTION(BlueprintCallable, Category = PersonaEditorExtensions)
	UDebugSkelMeshComponent* GetPreviewMeshComponent() const;

	/** Get the skeletal mesh that we are editing */
	UFUNCTION(BlueprintCallable, Category = PersonaEditorExtensions)
	USkeletalMesh* GetMesh() const;

	/** Get the anim blueprint that we are editing */
	UFUNCTION(BlueprintCallable, Category = PersonaEditorExtensions)
	UAnimBlueprint* GetAnimBlueprint() const;

	/** Get the animation asset that we are editing */
	UFUNCTION(BlueprintCallable, Category = PersonaEditorExtensions)
	UAnimationAsset* GetAnimationAsset() const;

	void SetToolkit(TSharedRef<IPersonaToolkit> InToolkit);

protected:
	bool HasValidToolkit() const;
private:
	TWeakPtr<IPersonaToolkit> WeakToolkit;
};
