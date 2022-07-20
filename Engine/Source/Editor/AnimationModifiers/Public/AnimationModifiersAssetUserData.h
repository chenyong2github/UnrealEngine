// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Engine/AssetUserData.h"
#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include "AnimationModifiersAssetUserData.generated.h"

class FArchive;
class UAnimationModifier;
class UObject;

/** Asset user data which can be added to a USkeleton or UAnimSequence to keep track of Animation Modifiers */
UCLASS()
class ANIMATIONMODIFIERS_API UAnimationModifiersAssetUserData : public UAssetUserData
{
	GENERATED_BODY()

	friend class SAnimationModifiersTab;
	friend class SAnimationModifierContentBrowserWindow;
	friend class FAnimationModifiersModule;
public:
	const TArray<UAnimationModifier*>& GetAnimationModifierInstances() const;
protected:	 
	/** Begin UAssetUserData overrides */
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;	
	virtual void Serialize(FArchive& Ar) override;
	/** End UAssetUserData overrides */

	void AddAnimationModifier(UAnimationModifier* Instance);
	void RemoveAnimationModifierInstance(UAnimationModifier* Instance);
	void ChangeAnimationModifierIndex(UAnimationModifier* Instance, int32 Direction);
private:
	void RemoveInvalidModifiers();
protected:
	UPROPERTY()
	TArray<TObjectPtr<UAnimationModifier>> AnimationModifierInstances;
};
