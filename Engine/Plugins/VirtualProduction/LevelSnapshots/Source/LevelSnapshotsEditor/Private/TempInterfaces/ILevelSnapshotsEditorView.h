// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UWorld;
class ILevelSnapshotsEditorContext;
class ULevelSnapshotsEditorData;
class ULevelSnapshot;

struct FLevelSnapshotsEditorViewBuilder
{
	TWeakPtr<ILevelSnapshotsEditorContext> EditorContextPtr;
	TWeakObjectPtr<ULevelSnapshotsEditorData> EditorDataPtr = nullptr;
};

class SWidget;

class ILevelSnapshotsEditorContext : public TSharedFromThis<ILevelSnapshotsEditorContext>
{
public:
	virtual ~ILevelSnapshotsEditorContext() { }

	virtual UWorld* Get() const = 0;
};

class ILevelSnapshotsEditorView : public TSharedFromThis<ILevelSnapshotsEditorView>
{

public:
	virtual ~ILevelSnapshotsEditorView() { }

	virtual TSharedRef<SWidget> GetOrCreateWidget() = 0;

	virtual TSharedRef<FLevelSnapshotsEditorViewBuilder> GetBuilder() const = 0;
};
