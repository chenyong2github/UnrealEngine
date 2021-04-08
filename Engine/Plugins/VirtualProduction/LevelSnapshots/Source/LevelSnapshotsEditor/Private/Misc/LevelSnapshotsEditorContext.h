// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILevelSnapshotsEditorView.h"

#include "Templates/SharedPointer.h"
#include "Delegates/Delegate.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;

class FLevelSnapshotsEditorContext : public ILevelSnapshotsEditorContext
{
public:

	FLevelSnapshotsEditorContext();
	~FLevelSnapshotsEditorContext();

	/**
	 * Resolve the current world context pointer. Can never be nullptr.
	 */
	virtual UWorld* Get() const override;

	/**
	 * Resolve the current world context pointer as a base object. Can never be nullptr.
	 */
	UObject* GetAsObject() const;

	/**
	 * Compute the new playback context based on the user's current auto-bind settings.
	 * Will use the first encountered PIE or Simulate world if possible, else the Editor world as a fallback
	 */
	static UWorld* ComputeContext();

	/**
	 * Specify a new world to use as the context. Persists until the next PIE or map change event.
	 * May be null, in which case the context will be recomputed automatically
	 */
	void OverrideWith(UWorld* InNewContext);

private:

	void OnPieEvent(bool);
	void OnMapChange(uint32);

private:

	/** Mutable cached context pointer */
	mutable TWeakObjectPtr<UWorld> WeakCurrentContext;
};