// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraSimCache.h"

struct FNiagaraSystemSimCacheCaptureReply;
struct FNiagaraSimCacheDataBuffersLayout;
class UNiagaraComponent;

class NIAGARAEDITOR_API FNiagaraSimCacheViewModel : public TSharedFromThis<FNiagaraSimCacheViewModel>
{
public:
	struct FComponentInfo
	{
		FName Name = NAME_None;
		uint32 ComponentOffset = INDEX_NONE;
		bool bIsFloat = false;
		bool bIsHalf = false;
		bool bIsInt32 = false;
		bool bShowAsBool = false;
		UEnum* Enum = nullptr;
	};

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnViewDataChanged, bool)
	
	FNiagaraSimCacheViewModel();
	~FNiagaraSimCacheViewModel();

	void Initialize(TWeakObjectPtr<UNiagaraSimCache> SimCache);

	void UpdateSimCache(const FNiagaraSystemSimCacheCaptureReply& Reply);

	void SetupPreviewComponentAndInstance();

	TConstArrayView<FComponentInfo> GetComponentInfos() const { return ComponentInfos; }

	int32 GetNumInstances() const { return NumInstances; }

	int32 GetNumFrames() const;

	int32 GetFrameIndex() const { return FrameIndex; };

	void SetFrameIndex(const int32 InFrameIndex);

	int32 GetEmitterIndex() const { return EmitterIndex; };

	void SetEmitterIndex(int32 InEmitterIndex);

	FText GetComponentText(FName ComponentName, int32 InstanceIndex) const;

	bool IsCacheValid();

	int32 GetNumEmitterLayouts();

	FName GetEmitterLayoutName(int32 Index);

	FOnViewDataChanged& OnViewDataChanged();

	void OnCacheModified(UNiagaraSimCache* SimCache);

	void UpdateCachedFrame();

	UNiagaraComponent* GetPreviewComponent() { return PreviewComponent; }

private:
	void BuildComponentInfos(const FName Name, const UScriptStruct* Struct);
	
	// The sim cache being viewed
	TWeakObjectPtr<UNiagaraSimCache>	WeakSimCache;

	// Component for preview scene
	UNiagaraComponent* PreviewComponent = nullptr;

	// Which frame of the cached sim is being viewed
	int32 FrameIndex = 0;
	
	// Which Emitter of the cached sim is being viewed
	int32 EmitterIndex = INDEX_NONE;

	// Number of particles in the given frame
	int32 NumInstances = 0;

	// Info about the variables in the cache
	TArray<FComponentInfo>				ComponentInfos;
	TArray<float>						FloatComponents;
	TArray<FFloat16>					HalfComponents;
	TArray<int32>						Int32Components;

	int32								FoundFloatComponents = 0;
	int32								FoundHalfComponents = 0;
	int32								FoundInt32Components = 0;

	bool								bDelegatesAdded = false;
	FOnViewDataChanged					OnViewDataChangedDelegate;
};
