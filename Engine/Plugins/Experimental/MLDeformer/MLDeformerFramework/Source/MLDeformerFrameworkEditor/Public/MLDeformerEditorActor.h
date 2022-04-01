// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

class UDebugSkelMeshComponent;
class UTextRenderComponent;
class UMLDeformerComponent;

namespace UE::MLDeformer
{
	enum : int32
	{
		ActorID_Train_Base,
		ActorID_Train_GroundTruth,
		ActorID_Test_Base,
		ActorID_Test_MLDeformed,
		ActorID_Test_GroundTruth
	};

	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerEditorActor
	{
	public:
		struct MLDEFORMERFRAMEWORKEDITOR_API FConstructSettings
		{
			AActor* Actor = nullptr;
			int32 TypeID = -1;
			FLinearColor LabelColor = FLinearColor(1.0f, 0.0f, 0.0f);
			FText LabelText;
			bool bIsTrainingActor = false;
		};
		FMLDeformerEditorActor(const FConstructSettings& Settings);
		virtual ~FMLDeformerEditorActor() = default;

		// Main methods you can override.
		virtual void SetVisibility(bool bIsVisible);
		virtual bool IsVisible() const;
		virtual void SetPlayPosition(float TimeInSeconds, bool bAutoPause=true);
		virtual float GetPlayPosition() const;
		virtual void SetPlaySpeed(float PlaySpeed);
		virtual FBox GetBoundingBox() const;
		virtual void Pause(bool bPaused);

		AActor* GetActor() const { return Actor; }
		int32 GetTypeID() const { return TypeID; }

		UDebugSkelMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent.Get(); }
		UTextRenderComponent* GetLabelComponent() const { return LabelComponent.Get(); }
		UMLDeformerComponent* GetMLDeformerComponent() const { return MLDeformerComponent.Get(); }
		void SetSkeletalMeshComponent(UDebugSkelMeshComponent* SkelMeshComponent) { SkeletalMeshComponent = SkelMeshComponent; }
		void SetMLDeformerComponent(UMLDeformerComponent* Component) { MLDeformerComponent = Component; }
		bool IsTrainingActor() const { return bIsTrainingActor; }
		bool IsTestActor() const { return !bIsTrainingActor; }
		void SetCanDestroyActor(bool bCanDestroy) { bCanDestroyActor = bCanDestroy; }
		bool GetCanDestroyActor() const { return bCanDestroyActor; }

	protected:
		UTextRenderComponent* CreateLabelComponent(AActor* InActor, FLinearColor Color, const FText& Text) const;

	protected:
		/**
		 * The ID of the editor actor type.
		 * This can be used to identify what actor we are dealing with, for example the base actor or ML Deformed one etc.
		 */
		int32 TypeID = -1;

		/** The label component, which shows above the actor. */
		TObjectPtr<UTextRenderComponent> LabelComponent = nullptr;

		/** The actual actor pointer. */
		TObjectPtr<AActor> Actor = nullptr;

		/** The skeletal mesh component (can be nullptr). */
		TObjectPtr<UDebugSkelMeshComponent> SkeletalMeshComponent = nullptr;

		/** The ML Deformer component (can be nullptr). */
		TObjectPtr<UMLDeformerComponent> MLDeformerComponent = nullptr;

		/** Can the actor we created be destroyed during cleanup? */
		bool bCanDestroyActor = true;

		/** Is this actor used for training? */
		bool bIsTrainingActor = true;
	};

}	// namespace UE::MLDeformer
