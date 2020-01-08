// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRig.h"
#include "Animation/InputScaleBias.h"
#include "AnimNode_ControlRigBase.h"
#include "AnimNode_ControlRig.generated.h"

class UNodeMappingContainer;

/**
 * Animation node that allows animation ControlRig output to be used in an animation graph
 */
USTRUCT()
struct CONTROLRIG_API FAnimNode_ControlRig : public FAnimNode_ControlRigBase
{
	GENERATED_BODY()

	FAnimNode_ControlRig();
	virtual ~FAnimNode_ControlRig();

	UControlRig* GetControlRig() const { return ControlRig; }

	// FAnimNode_Base interface
	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext & Output) override;

	void SetIOMapping(bool bInput, const FName& SourceProperty, const FName& TargetCurve);
	FName GetIOMapping(bool bInput, const FName& SourceProperty) const;
private:

	/** Cached ControlRig */
	UPROPERTY(EditAnywhere, Category = ControlRig)
	TSubclassOf<UControlRig> ControlRigClass;

	UPROPERTY(transient)
	UControlRig* ControlRig;

	// alpha value handler
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinShownByDefault))
	float Alpha;

	UPROPERTY(EditAnywhere, Category = Settings)
	EAnimAlphaInputType AlphaInputType;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinShownByDefault, DisplayName = "bEnabled", DisplayAfter = "AlphaScaleBias"))
	uint8 bAlphaBoolEnabled : 1;

	UPROPERTY(EditAnywhere, Category=Settings)
	FInputScaleBias AlphaScaleBias;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (DisplayName = "Blend Settings"))
	FInputAlphaBoolBlend AlphaBoolBlend;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinShownByDefault))
	FName AlphaCurveName;

	UPROPERTY(EditAnywhere, Category = Settings)
	FInputScaleBiasClamp AlphaScaleBiasClamp;

	// we only save mapping, 
	// we have to query control rig when runtime 
	// to ensure type and everything is still valid or not
	UPROPERTY()
	TMap<FName, FName> InputMapping;

	UPROPERTY()
	TMap<FName, FName> OutputMapping;

	TMap<FName, FName> InputTypes;
	TMap<FName, FName> OutputTypes;

#if WITH_EDITOR
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap);
#endif // WITH_EDITOR
protected:
	virtual UClass* GetTargetClass() const override { return *ControlRigClass; } 
	virtual void UpdateInput(UControlRig* InControlRig, const FPoseContext& InOutput) override;
	virtual void UpdateOutput(UControlRig* InControlRig, FPoseContext& InOutput) override;
public:
	void PostSerialize(const FArchive& Ar);

	friend class UAnimGraphNode_ControlRig;
};

template<>
struct TStructOpsTypeTraits<FAnimNode_ControlRig> : public TStructOpsTypeTraitsBase2<FAnimNode_ControlRig>
{
	enum
	{
		WithPostSerialize = true,
	};
};