// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/PropertyViewBase.h"

#include "Async/Async.h"
#include "Editor.h"
#include "Engine/World.h"
#include "PropertyEditorModule.h"
#include "Widgets/Layout/SBorder.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UPropertyViewBase


void UPropertyViewBase::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	FCoreUObjectDelegates::OnAssetLoaded.Remove(AssetLoadedHandle);
	AssetLoadedHandle.Reset();
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	PostLoadMapHandle.Reset();
	FEditorDelegates::MapChange.Remove(MapChangeHandle);
	MapChangeHandle.Reset();

	DisplayedWidget.Reset();
}


TSharedRef<SWidget> UPropertyViewBase::RebuildWidget()
{
	DisplayedWidget = SNew(SBorder)
		.Padding(0.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"));
	
	BuildContentWidget();

	if (!AssetLoadedHandle.IsValid())
	{
		AssetLoadedHandle = FCoreUObjectDelegates::OnAssetLoaded.AddUObject(this, &UPropertyViewBase::InternalOnAssetLoaded);
	}
	if (!PostLoadMapHandle.IsValid())
	{
		PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UPropertyViewBase::InternalPostLoadMapWithWorld);
	}
	if (!MapChangeHandle.IsValid())
	{
		MapChangeHandle = FEditorDelegates::MapChange.AddUObject(this, &UPropertyViewBase::InternalOnMapChange);
	}

	return DisplayedWidget.ToSharedRef();
}


UObject* UPropertyViewBase::GetObject() const
{
	return LazyObject.Get();
}


void UPropertyViewBase::SetObject(UObject* InObject)
{
	if (LazyObject.Get() != InObject)
	{
		LazyObject = InObject;
		SoftObjectPath = InObject;
		OnObjectChanged();
	}
}


void UPropertyViewBase::OnPropertyChangedBroadcast(FName PropertyName)
{
	OnPropertyChanged.Broadcast(PropertyName);
}


void UPropertyViewBase::InternalOnAssetLoaded(UObject* AssetLoaded)
{
	if (SoftObjectPath.GetAssetPathName() == FSoftObjectPath(AssetLoaded).GetAssetPathName())
	{
		BuildContentWidget();
	}
}


void UPropertyViewBase::InternalPostLoadMapWithWorld(UWorld* AssetLoaded)
{
	InternalOnMapChange(0);
}


void UPropertyViewBase::InternalOnMapChange(uint32)
{
	BuildContentWidget();
}


void UPropertyViewBase::PostLoad()
{
	Super::PostLoad();

	if (!LazyObject.IsValid() && SoftObjectPath.IsAsset() && bAutoLoadAsset && !HasAnyFlags(RF_BeginDestroyed))
	{
		LazyObject = SoftObjectPath.TryLoad();
		BuildContentWidget();
	}
}


void UPropertyViewBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UPropertyViewBase, LazyObject))
	{
		SoftObjectPath = LazyObject.Get();
		OnObjectChanged();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}


const FText UPropertyViewBase::GetPaletteCategory()
{
	return LOCTEXT("Editor", "Editor");
}

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
