// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioExtensionPlugin.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "SoundEffectBase.h"

#include "SoundEffectPreset.generated.h"

// Forward Declarations
struct FAssetData;

class FMenuBuilder;
class FSoundEffectBase;
class IToolkitHost;


UCLASS(config = Engine, abstract, editinlinenew, BlueprintType)
class ENGINE_API USoundEffectPreset : public UObject
{
	GENERATED_BODY()

public:
	USoundEffectPreset(const FObjectInitializer& ObjectInitializer);
	virtual ~USoundEffectPreset() = default;


	virtual bool CanFilter() const { return true; }
	virtual FText GetAssetActionName() const PURE_VIRTUAL(USoundEffectPreset::GetAssetActionName, return FText(););
	virtual UClass* GetSupportedClass() const PURE_VIRTUAL(USoundEffectPreset::GetSupportedClass, return nullptr;);
	virtual USoundEffectPreset* CreateNewPreset(UObject* InParent, FName Name, EObjectFlags Flags) const PURE_VIRTUAL(USoundEffectPreset::CreateNewPreset, return nullptr;);
	virtual FSoundEffectBase* CreateNewEffect() const PURE_VIRTUAL(USoundEffectPreset::CreateNewEffect, return nullptr;);
	virtual bool HasAssetActions() const { return false; }
	virtual void Init() PURE_VIRTUAL(USoundEffectPreset::Init, );
	virtual void OnInit() {};
	virtual FColor GetPresetColor() const { return FColor(200.0f, 100.0f, 100.0f); }

	void Update();
	void AddEffectInstance(TSoundEffectPtr& InEffectPtr);
	void RemoveEffectInstance(TSoundEffectPtr& InEffectPtr);

	void AddReferencedEffects(FReferenceCollector& InCollector);

	virtual void BeginDestroy() override;

protected:
#if WITH_EDITORONLY_DATA
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	using TSoundEffectWeakPtr = TWeakPtr<FSoundEffectBase, ESPMode::ThreadSafe>;

	// Array of instances which are using this preset
	TArray<TSoundEffectWeakPtr> Instances;
	bool bInitialized;

	/* Immediately executes command for each active effect instance on the active thread */
	template <typename T>
	void IterateEffects(TFunction<void(T&)> InForEachEffect)
	{
		for (TSoundEffectWeakPtr& Instance : Instances)
		{
			TSoundEffectPtr EffectStrongPtr = Instance.Pin();
			if (EffectStrongPtr.IsValid())
			{
				InForEachEffect(*static_cast<T*>(EffectStrongPtr.Get()));
			}
		}
	}

	/* Defers execution of command on each active effect instance on the audio render thread */
	template <typename T>
	void EffectCommand(TFunction<void(T&)> InForEachEffect)
	{
		IterateEffects<T>([InForEachEffect](T& OutInstance)
		{
			T* InstancePtr = &OutInstance;
			OutInstance.EffectCommand([InstancePtr, InForEachEffect]()
			{
				InForEachEffect(*InstancePtr);
			});
		});
	}

public:
	template <typename TInitData, typename TSoundEffectType>
	static TSharedPtr<TSoundEffectType, ESPMode::ThreadSafe> CreateInstance(const TInitData& InInitData, USoundEffectPreset& InOutPreset)
	{
		TSoundEffectType* NewEffect = static_cast<TSoundEffectType*>(InOutPreset.CreateNewEffect());
		NewEffect->Preset = &InOutPreset;
		NewEffect->Init(InInitData);

		TSharedPtr<TSoundEffectType, ESPMode::ThreadSafe> NewEffectPtr(NewEffect);

		TSoundEffectPtr SoundEffectPtr = StaticCastSharedPtr<FSoundEffectBase, TSoundEffectType, ESPMode::ThreadSafe>(NewEffectPtr);
		InOutPreset.AddEffectInstance(SoundEffectPtr);

		return NewEffectPtr;
	}

	static void UnregisterInstance(TSoundEffectPtr InEffectPtr)
	{
		if (InEffectPtr.IsValid())
		{
			if (USoundEffectPreset* Preset = InEffectPtr->GetPreset())
			{
				Preset->RemoveEffectInstance(InEffectPtr);
			}

			InEffectPtr->ClearPreset();
		}
	}

	static void RegisterInstance(USoundEffectPreset& InPreset, TSoundEffectPtr InEffectPtr)
	{
		if (!InEffectPtr.IsValid())
		{
			return;
		}

		if (InEffectPtr->Preset.Get() != &InPreset)
		{
			UnregisterInstance(InEffectPtr);

			InEffectPtr->Preset = &InPreset;
			if (InEffectPtr->Preset.IsValid())
			{
				InPreset.AddEffectInstance(InEffectPtr);
			}
		}

		// Anytime notification occurs that the preset has been modified,
		// flag for update.
		InEffectPtr->bChanged = true;
	}
};