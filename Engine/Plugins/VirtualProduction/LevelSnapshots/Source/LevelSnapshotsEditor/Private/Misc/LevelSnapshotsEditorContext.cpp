// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/LevelSnapshotsEditorContext.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

FLevelSnapshotsEditorContext::FLevelSnapshotsEditorContext()
{
	FEditorDelegates::MapChange.AddRaw(this, &FLevelSnapshotsEditorContext::OnMapChange);
	FEditorDelegates::PreBeginPIE.AddRaw(this, &FLevelSnapshotsEditorContext::OnPieEvent);
	FEditorDelegates::BeginPIE.AddRaw(this, &FLevelSnapshotsEditorContext::OnPieEvent);
	FEditorDelegates::PostPIEStarted.AddRaw(this, &FLevelSnapshotsEditorContext::OnPieEvent);
	FEditorDelegates::PrePIEEnded.AddRaw(this, &FLevelSnapshotsEditorContext::OnPieEvent);
	FEditorDelegates::EndPIE.AddRaw(this, &FLevelSnapshotsEditorContext::OnPieEvent);
}

FLevelSnapshotsEditorContext::~FLevelSnapshotsEditorContext()
{
	FEditorDelegates::MapChange.RemoveAll(this);
	FEditorDelegates::PreBeginPIE.RemoveAll(this);
	FEditorDelegates::BeginPIE.RemoveAll(this);
	FEditorDelegates::PostPIEStarted.RemoveAll(this);
	FEditorDelegates::PrePIEEnded.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
}

void FLevelSnapshotsEditorContext::OnPieEvent(bool)
{
	WeakCurrentContext = nullptr;
}

void FLevelSnapshotsEditorContext::OnMapChange(uint32)
{
	WeakCurrentContext = nullptr;
}

UWorld* FLevelSnapshotsEditorContext::Get() const
{
	UWorld* Context = WeakCurrentContext.Get();
	if (Context)
	{
		return Context;
	}

	Context = ComputeContext();
	check(Context);
	WeakCurrentContext = Context;
	return Context;
}

UObject* FLevelSnapshotsEditorContext::GetAsObject() const
{
	return Get();
}

void FLevelSnapshotsEditorContext::OverrideWith(UWorld* InNewContext)
{
	WeakCurrentContext = InNewContext;
}

UWorld* FLevelSnapshotsEditorContext::ComputeContext()
{
	UWorld* EditorWorld = nullptr;

	// Return PIE worlds if there are any
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE)
		{
			UWorld* ThisWorld = Context.World();
			if (GEditor && GEditor->bIsSimulatingInEditor)
			{
				return ThisWorld;
			}
		}
		else if (Context.WorldType == EWorldType::Editor)
		{
			EditorWorld = Context.World();
		}
	}

	check(EditorWorld);

	return EditorWorld;
}

#undef LOCTEXT_NAMESPACE
