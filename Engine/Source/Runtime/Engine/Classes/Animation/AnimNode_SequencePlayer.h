// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/InputScaleBias.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimNode_SequencePlayer.generated.h"


// Sequence player node. Not instantiated directly, use FAnimNode_SequencePlayer or FAnimNode_SequencePlayer_Standalone
USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FAnimNode_SequencePlayerBase : public FAnimNode_AssetPlayerBase
{
	GENERATED_BODY()

protected:
	// Corresponding state for PlayRateScaleBiasClampConstants
	UPROPERTY()
	FInputScaleBiasClampState PlayRateScaleBiasClampState;

public:
	// FAnimNode_AssetPlayerBase interface
	virtual float GetCurrentAssetTime() const override;
	virtual float GetCurrentAssetTimePlayRateAdjusted() const override;
	virtual float GetCurrentAssetLength() const override;
	virtual UAnimationAsset* GetAnimAsset() const override { return GetSequence(); }
	// End of FAnimNode_AssetPlayerBase interface

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	float GetTimeFromEnd(float CurrentNodeTime) const;
	float GetEffectiveStartPosition(const FAnimationBaseContext& Context) const;

	// The animation sequence asset to play
	virtual UAnimSequenceBase* GetSequence() const { return nullptr; }

protected:
	// Set the animation sequence asset to play
	virtual void SetSequence(UAnimSequenceBase* InSequence) PURE_VIRTUAL(SetSequence, )

	// Set the animation to continue looping when it reaches the end
	virtual void SetLoopAnimation(bool bInLoopAnimation) PURE_VIRTUAL(SetLoopAnimation, )
	
	// The Basis in which the PlayRate is expressed in. This is used to rescale PlayRate inputs.
	// For example a Basis of 100 means that the PlayRate input will be divided by 100.
	virtual float GetPlayRateBasis() const PURE_VIRTUAL(GetPlayRateBasis, return 1.0f;)

	// The play rate multiplier. Can be negative, which will cause the animation to play in reverse.
	virtual float GetPlayRate() const PURE_VIRTUAL(GetPlayRate, return 1.0f;)
	
	// Additional scaling, offsetting and clamping of PlayRate input.
	// Performed after PlayRateBasis.
	virtual const FInputScaleBiasClampConstants& GetPlayRateScaleBiasClampConstants() const PURE_VIRTUAL(GetPlayRateScaleBiasClampConstants, static FInputScaleBiasClampConstants Dummy; return Dummy; )

	// The start up position, it only applies when reinitialized
	// if you loop, it will still start from 0.f after finishing the round
	virtual float GetStartPosition() const PURE_VIRTUAL(GetStartPosition, return 0.0f;)

	// Should the animation continue looping when it reaches the end?
	virtual bool GetLoopAnimation() const PURE_VIRTUAL(GetLoopAnimation, return true;)

	// Use pose matching to choose the start position. Requires experimental PoseSearch plugin.
	virtual bool GetStartFromMatchingPose() const PURE_VIRTUAL(GetStartFromMatchingPose, return false;)
};

// Sequence player node that can be used with constant folding
USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FAnimNode_SequencePlayer : public FAnimNode_SequencePlayerBase
{
	GENERATED_BODY()

protected:
	friend class UAnimGraphNode_SequencePlayer;

#if WITH_EDITORONLY_DATA
	// The animation sequence asset to play
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, DisallowedClasses="AnimMontage", FoldProperty))
	TObjectPtr<UAnimSequenceBase> Sequence = nullptr;

	// The Basis in which the PlayRate is expressed in. This is used to rescale PlayRate inputs.
	// For example a Basis of 100 means that the PlayRate input will be divided by 100.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, FoldProperty))
	float PlayRateBasis = 1.0f;

	// The play rate multiplier. Can be negative, which will cause the animation to play in reverse.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, FoldProperty))
	float PlayRate = 1.0f;
	
	// Additional scaling, offsetting and clamping of PlayRate input.
	// Performed after PlayRateBasis.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (DisplayName="PlayRateScaleBiasClamp", FoldProperty))
	FInputScaleBiasClampConstants PlayRateScaleBiasClampConstants;

	UPROPERTY()
	FInputScaleBiasClamp PlayRateScaleBiasClamp_DEPRECATED;

	// The start up position, it only applies when reinitialized
	// if you loop, it will still start from 0.f after finishing the round
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, FoldProperty))
	float StartPosition = 0.0f;

	// Should the animation continue looping when it reaches the end?
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, FoldProperty))
	bool bLoopAnimation = true;

	// Use pose matching to choose the start position. Requires experimental PoseSearch plugin.
	UPROPERTY(EditAnywhere, Category = PoseMatching, meta = (PinHiddenByDefault, FoldProperty))
	bool bStartFromMatchingPose = false;
#endif

public:
	// FAnimNode_SequencePlayerBase interface
	virtual void SetSequence(UAnimSequenceBase* InSequence) override;
	virtual UAnimSequenceBase* GetSequence() const override;
	virtual float GetPlayRateBasis() const override;
	virtual float GetPlayRate() const override;
	virtual const FInputScaleBiasClampConstants& GetPlayRateScaleBiasClampConstants() const override;
	virtual float GetStartPosition() const override;
	virtual bool GetLoopAnimation() const override;
	virtual bool GetStartFromMatchingPose() const override;
};

// Sequence player node that can be used standalone (without constant folding)
USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FAnimNode_SequencePlayer_Standalone : public FAnimNode_SequencePlayerBase
{	
	GENERATED_BODY()

protected:
	// The animation sequence asset to play
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, DisallowedClasses="AnimMontage"))
	TObjectPtr<UAnimSequenceBase> Sequence = nullptr;

	// The Basis in which the PlayRate is expressed in. This is used to rescale PlayRate inputs.
	// For example a Basis of 100 means that the PlayRate input will be divided by 100.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	float PlayRateBasis = 1.0f;

	// The play rate multiplier. Can be negative, which will cause the animation to play in reverse.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	float PlayRate = 1.0f;
	
	// Additional scaling, offsetting and clamping of PlayRate input.
	// Performed after PlayRateBasis.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (DisplayName="PlayRateScaleBiasClamp"))
	FInputScaleBiasClampConstants PlayRateScaleBiasClampConstants;

	// The start up position, it only applies when reinitialized
	// if you loop, it will still start from 0.f after finishing the round
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	float StartPosition = 0.0f;

	// Should the animation continue looping when it reaches the end?
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	bool bLoopAnimation = true;

	// Use pose matching to choose the start position. Requires experimental PoseSearch plugin.
	UPROPERTY(EditAnywhere, Category = PoseMatching, meta = (PinHiddenByDefault))
	bool bStartFromMatchingPose = false;

public:
	// FAnimNode_SequencePlayerBase interface
	virtual void SetSequence(UAnimSequenceBase* InSequence) override;
	virtual void SetLoopAnimation(bool bInLoopAnimation) override;
	virtual UAnimSequenceBase* GetSequence() const override;
	virtual float GetPlayRateBasis() const override;
	virtual float GetPlayRate() const override;
	virtual const FInputScaleBiasClampConstants& GetPlayRateScaleBiasClampConstants() const override;
	virtual float GetStartPosition() const override;
	virtual bool GetLoopAnimation() const override;
	virtual bool GetStartFromMatchingPose() const override;
};
