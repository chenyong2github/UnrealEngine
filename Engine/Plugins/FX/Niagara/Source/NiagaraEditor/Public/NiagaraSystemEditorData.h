// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEditorDataBase.h"
#include "NiagaraSystemEditorData.generated.h"

class UNiagaraStackEditorData;
class UNiagaraSystem;
class UEdGraph;

/** Editor only folder data for emitters in a system. */
UCLASS()
class UNiagaraSystemEditorFolder : public UObject
{
	GENERATED_BODY()

public:
	const FName GetFolderName() const;

	void SetFolderName(FName InFolderName);

	const TArray<UNiagaraSystemEditorFolder*>& GetChildFolders() const;

	void AddChildFolder(UNiagaraSystemEditorFolder* ChildFolder);

	void RemoveChildFolder(UNiagaraSystemEditorFolder* ChildFolder);

	const TArray<FGuid>& GetChildEmitterHandleIds() const;

	void AddChildEmitterHandleId(FGuid ChildEmitterHandleId);

	void RemoveChildEmitterHandleId(FGuid ChildEmitterHandleId);

private:
	UPROPERTY()
	FName FolderName;

	UPROPERTY()
	TArray<UNiagaraSystemEditorFolder*> ChildFolders;

	UPROPERTY()
	TArray<FGuid> ChildEmitterHandleIds;
};

/** Editor only UI data for systems. */
UCLASS()
class NIAGARAEDITOR_API UNiagaraSystemEditorData : public UNiagaraEditorDataBase
{
	GENERATED_BODY()

public:
	UNiagaraSystemEditorData(const FObjectInitializer& ObjectInitializer);

	//~ Begin UObject Interface
	void PostInitProperties();
	//~ End UObject Interface

	virtual void PostLoadFromOwner(UObject* InOwner) override;

	/** Gets the root folder for UI folders for emitters. */
	UNiagaraSystemEditorFolder& GetRootFolder() const;

	/** Gets the stack editor data for the system. */
	UNiagaraStackEditorData& GetStackEditorData() const;

	const FTransform& GetOwnerTransform() const {
		return OwnerTransform;
	}

	void SetOwnerTransform(const FTransform& InTransform) {
		OwnerTransform = InTransform;
	}

	TRange<float> GetPlaybackRange() const;

	void SetPlaybackRange(TRange<float> InPlaybackRange);

	UEdGraph* GetSystemOverviewGraph() const;

	const FNiagaraGraphViewSettings& GetSystemOverviewGraphViewSettings() const;

	void SetSystemOverviewGraphViewSettings(const FNiagaraGraphViewSettings& InOverviewGraphViewSettings);

	bool GetOwningSystemIsPlaceholder() const;

	void SetOwningSystemIsPlaceholder(bool bInSystemIsPlaceholder, UNiagaraSystem& OwnerSystem);

	void SynchronizeOverviewGraphWithSystem(UNiagaraSystem& OwnerSystem);

private:
	void UpdatePlaybackRangeFromEmitters(UNiagaraSystem& OwnerSystem);

private:
	UPROPERTY(Instanced)
	UNiagaraSystemEditorFolder* RootFolder;

	UPROPERTY(Instanced)
	UNiagaraStackEditorData* StackEditorData;

	UPROPERTY()
	FTransform OwnerTransform;

	UPROPERTY()
	float PlaybackRangeMin;

	UPROPERTY()
	float PlaybackRangeMax;

	/** Graph presenting overview of the current system and its emitters. */
	UPROPERTY()
	UEdGraph* SystemOverviewGraph;

	UPROPERTY()
	FNiagaraGraphViewSettings OverviewGraphViewSettings;

	UPROPERTY()
	bool bSystemIsPlaceholder;
};
