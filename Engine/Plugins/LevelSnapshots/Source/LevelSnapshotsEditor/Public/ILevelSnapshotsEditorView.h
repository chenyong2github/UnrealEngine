// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;
class ILevelSnapshotsEditorContext;

struct FLevelSnapshotsEditorViewBuilder
{
	TWeakPtr<ILevelSnapshotsEditorContext> EditorContextPtr;
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

	virtual const FLevelSnapshotsEditorViewBuilder& GetBuilder() const = 0;
};
