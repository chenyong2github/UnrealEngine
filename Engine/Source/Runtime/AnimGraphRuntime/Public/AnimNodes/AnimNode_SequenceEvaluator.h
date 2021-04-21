// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimNode_SequenceEvaluator.generated.h"

UENUM(BlueprintType)
namespace ESequenceEvalReinit
{
	enum Type
	{
		/** Do not reset InternalTimeAccumulator */
		NoReset,
		/** Reset InternalTimeAccumulator to StartPosition */
		StartPosition,
		/** Reset InternalTimeAccumulator to ExplicitTime */
		ExplicitTime,
	};
}

// Abstract base class. Evaluates a point in an anim sequence, using a specific time input rather than advancing time internally.
// Typically the playback position of the animation for this node will represent something other than time, like jump height.
// This node will not trigger any notifies present in the associated sequence.
USTRUCT(BlueprintInternalUseOnly)
struct ANIMGRAPHRUNTIME_API FAnimNode_SequenceEvaluatorBase : public FAnimNode_AssetPlayerBase
{
	GENERATED_BODY()

private:
	bool bReinitialized = false;

public:
	// FAnimNode_AssetPlayerBase interface
	virtual float GetCurrentAssetTime() const override;
	virtual float GetCurrentAssetLength() const override;
	// End of FAnimNode_AssetPlayerBase interface

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_AssetPlayerBase Interface
	virtual float GetAccumulatedTime() const {return GetExplicitTime();}
	virtual void SetAccumulatedTime(const float& NewTime) { SetExplicitTime(NewTime); }
	virtual UAnimationAsset* GetAnimAsset() const { return GetSequence(); }
	// End of FAnimNode_AssetPlayerBase Interface

	void SetExplicitPreviousTime(float PreviousTime) { InternalTimeAccumulator = PreviousTime; }

	// Set the animation sequence asset to evaluate
	virtual void SetSequence(UAnimSequenceBase* InSequence) PURE_VIRTUAL(SetSequence, )

	// Set the time at which to evaluate the associated sequence
	virtual void SetExplicitTime(float InTime) PURE_VIRTUAL(SetExplicitTime, )

	// Set whether to teleport to explicit time when it is set
	virtual void SetTeleportToExplicitTime(bool bInTeleport) PURE_VIRTUAL(SetTeleportToExplicitTime, )

	/** Set what to do when SequenceEvaluator is reinitialized */
	virtual void SetReinitializationBehavior(TEnumAsByte<ESequenceEvalReinit::Type> InBehavior) PURE_VIRTUAL(SetReinitializationBehavior, )

	// The animation sequence asset to evaluate
	virtual UAnimSequenceBase* GetSequence() const PURE_VIRTUAL(GetSequence, return nullptr;)

	// The time at which to evaluate the associated sequence
	virtual float GetExplicitTime() const PURE_VIRTUAL(GetExplicitTime,  return 0.0f;)

	/** This only works if bTeleportToExplicitTime is false OR this node is set to use SyncGroup */
	virtual bool GetShouldLoop() const PURE_VIRTUAL(GetShouldLoop, return true;)

	/** If true, teleport to explicit time, does NOT advance time (does not trigger notifies, does not extract Root Motion, etc.)
	If false, will advance time (will trigger notifies, extract root motion if applicable, etc.)
	Note: using a sync group forces advancing time regardless of what this option is set to. */
	virtual bool GetTeleportToExplicitTime() const PURE_VIRTUAL(GetTeleportToExplicitTime, return true;)

	/** What to do when SequenceEvaluator is reinitialized */
	virtual TEnumAsByte<ESequenceEvalReinit::Type> GetReinitializationBehavior() const PURE_VIRTUAL(GetReinitializationBehavior, return ESequenceEvalReinit::ExplicitTime;)

	// The start up position, it only applies when ReinitializationBehavior == StartPosition. Only used when bTeleportToExplicitTime is false.
	virtual float GetStartPosition() const PURE_VIRTUAL(GetStartPosition,  return 0.0f;)
};

// Sequence evaluator node that can be used with constant folding
USTRUCT(BlueprintInternalUseOnly)
struct ANIMGRAPHRUNTIME_API FAnimNode_SequenceEvaluator : public FAnimNode_SequenceEvaluatorBase
{
	GENERATED_BODY()

private:
	friend class UAnimGraphNode_SequenceEvaluator;

#if WITH_EDITORONLY_DATA
	// The animation sequence asset to evaluate
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, FoldProperty))
	TObjectPtr<UAnimSequenceBase> Sequence = nullptr;

	// The time at which to evaluate the associated sequence
	UPROPERTY(EditAnywhere, Category=Settings, meta=(PinShownByDefault, FoldProperty))
	float ExplicitTime = 0.0f;

	/** This only works if bTeleportToExplicitTime is false OR this node is set to use SyncGroup */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, FoldProperty))
	bool bShouldLoop = true;

	/** If true, teleport to explicit time, does NOT advance time (does not trigger notifies, does not extract Root Motion, etc.)
	If false, will advance time (will trigger notifies, extract root motion if applicable, etc.)
	Note: using a sync group forces advancing time regardless of what this option is set to. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, FoldProperty))
	bool bTeleportToExplicitTime = true;

	/** What to do when SequenceEvaluator is reinitialized */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayAfter="StartPosition", FoldProperty))
	TEnumAsByte<ESequenceEvalReinit::Type> ReinitializationBehavior = ESequenceEvalReinit::ExplicitTime;

	// The start up position, it only applies when ReinitializationBehavior == StartPosition. Only used when bTeleportToExplicitTime is false.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, FoldProperty))
	float StartPosition = 0.0f;
#endif

public:
	// FAnimNode_SequenceEvaluatorBase interface
	virtual void SetSequence(UAnimSequenceBase* InSequence) override;
	virtual UAnimSequenceBase* GetSequence() const override;
	virtual float GetExplicitTime() const override;
	virtual bool GetShouldLoop() const override;
	virtual bool GetTeleportToExplicitTime() const override;
	virtual TEnumAsByte<ESequenceEvalReinit::Type> GetReinitializationBehavior() const override;
	virtual float GetStartPosition() const override;
};

// Sequence evaluator node that can be used standalone (without constant folding)
USTRUCT(BlueprintInternalUseOnly)
struct ANIMGRAPHRUNTIME_API FAnimNode_SequenceEvaluator_Standalone : public FAnimNode_SequenceEvaluatorBase
{
	GENERATED_BODY()

private:
	// The animation sequence asset to evaluate
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	TObjectPtr<UAnimSequenceBase> Sequence = nullptr;

	// The time at which to evaluate the associated sequence
	UPROPERTY(EditAnywhere, Category=Settings, meta=(PinShownByDefault))
	float ExplicitTime = 0.0f;

	/** This only works if bTeleportToExplicitTime is false OR this node is set to use SyncGroup */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	bool bShouldLoop = true;

	/** If true, teleport to explicit time, does NOT advance time (does not trigger notifies, does not extract Root Motion, etc.)
	If false, will advance time (will trigger notifies, extract root motion if applicable, etc.)
	Note: using a sync group forces advancing time regardless of what this option is set to. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	bool bTeleportToExplicitTime = true;

	/** What to do when SequenceEvaluator is reinitialized */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayAfter="StartPosition"))
	TEnumAsByte<ESequenceEvalReinit::Type> ReinitializationBehavior = ESequenceEvalReinit::ExplicitTime;

	// The start up position, it only applies when ReinitializationBehavior == StartPosition. Only used when bTeleportToExplicitTime is false.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	float StartPosition = 0.0f;

public:
	// FAnimNode_SequenceEvaluatorBase interface
	virtual void SetSequence(UAnimSequenceBase* InSequence) override { Sequence = InSequence; }
	virtual void SetExplicitTime(float InTime) override { ExplicitTime = InTime; }
	virtual void SetTeleportToExplicitTime(bool bInTeleport) override { bTeleportToExplicitTime = bInTeleport; }
	virtual void SetReinitializationBehavior(TEnumAsByte<ESequenceEvalReinit::Type> InBehavior) override { ReinitializationBehavior = InBehavior; }
	virtual UAnimSequenceBase* GetSequence() const override { return Sequence; }
	virtual float GetExplicitTime() const override { return ExplicitTime; }
	virtual bool GetShouldLoop() const override { return bShouldLoop; }
	virtual bool GetTeleportToExplicitTime() const override { return bTeleportToExplicitTime; }
	virtual TEnumAsByte<ESequenceEvalReinit::Type> GetReinitializationBehavior() const override { return ReinitializationBehavior; }
	virtual float GetStartPosition() const override { return StartPosition; }
};