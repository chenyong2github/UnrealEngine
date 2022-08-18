// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UNiagaraSimCache;
struct FNiagaraSystemSimCacheCaptureReply;
struct FNiagaraSimCacheDataBuffersLayout;

struct FNiagaraSimCacheComponentInfo
{
	FName Name = NAME_None;
	uint32 ComponentOffset = INDEX_NONE;
	bool bIsFloat = false;
	bool bIsHalf = false;
	bool bIsInt32 = false;
	bool bShowAsBool = false;
	UEnum* Enum = nullptr;
};

class NIAGARAEDITOR_API FNiagaraSimCacheViewModel : public TSharedFromThis<FNiagaraSimCacheViewModel>
{
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnFrameUpdated, bool)
	
	FNiagaraSimCacheViewModel();
	~FNiagaraSimCacheViewModel();

	void Initialize(TWeakObjectPtr<UNiagaraSimCache> SimCache);

	void UpdateSimCache(const FNiagaraSystemSimCacheCaptureReply& Reply);

	TConstArrayView<FNiagaraSimCacheComponentInfo> GetComponentInfos() const;

	int32 GetNumInstances() const;

	int32 GetNumFrames() const;

	int32 GetFrameIndex() const { return FrameIndex; };

	void SetFrameIndex(const int32 InFrameIndex);;

	int32 GetEmitterIndex() const { return EmitterIndex; };

	void SetEmitterIndex(int32 InEmitterIndex);

	const FNiagaraSimCacheDataBuffersLayout* GetSimCacheBufferLayout() const;

	FText GetComponentText(FName ComponentName, int32 InstanceIndex) const;

	bool IsCacheValid();

	int32 GetNumEmitterLayouts();

	FName GetEmitterLayoutName(int32 Index);

	FOnFrameUpdated& OnFrameUpdated();

private:
	uint32 FrameIndex = 0;
	uint32 EmitterIndex = INDEX_NONE;

	FOnFrameUpdated OnFrameUpdatedDelegate;
};