// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerInstance.h"

#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"


#define LOCTEXT_NAMESPACE "DataLayer"

UDataLayerInstance::UDataLayerInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)

{

}

void UDataLayerInstance::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	// Initialize bIsVisible with persistent flag bIsInitiallyVisible
	bIsVisible = bIsInitiallyVisible;
#endif

	if (Parent)
	{
		Parent->AddChild(this);
	}
}

#if WITH_EDITOR
void UDataLayerInstance::SetVisible(bool bInIsVisible)
{
	if (bIsVisible != bInIsVisible)
	{
		Modify(/*bAlwaysMarkDirty*/false);
		bIsVisible = bInIsVisible;
	}
}

void UDataLayerInstance::SetIsInitiallyVisible(bool bInIsInitiallyVisible)
{
	if (bIsInitiallyVisible != bInIsInitiallyVisible)
	{
		Modify();
		bIsInitiallyVisible = bInIsInitiallyVisible;
	}
}

void UDataLayerInstance::SetIsLoadedInEditor(bool bInIsLoadedInEditor, bool bInFromUserChange)
{
	if (bIsLoadedInEditor != bInIsLoadedInEditor)
	{
		Modify(false);
		bIsLoadedInEditor = bInIsLoadedInEditor;
		bIsLoadedInEditorChangedByUserOperation |= bInFromUserChange;
	}
}

void UDataLayerInstance::OnCreated(const UDataLayerAsset* Asset)
{
	Modify(/*bAlwaysMarkDirty*/false);

	check(DataLayerAsset == nullptr);
	DataLayerAsset = Asset;

	bIsVisible = true;
}

bool UDataLayerInstance::IsEffectiveLoadedInEditor() const
{
	bool bResult = IsLoadedInEditor();

	const UDataLayerInstance* ParentDataLayer = GetParent();
	while (ParentDataLayer && bResult)
	{
		bResult = bResult && ParentDataLayer->IsLoadedInEditor();
		ParentDataLayer = ParentDataLayer->GetParent();
	}
	return bResult;
}

bool UDataLayerInstance::IsLocked() const
{
	if (bIsLocked)
	{
		return true;
	}

	return IsRuntime() && !GetOuterAWorldDataLayers()->GetAllowRuntimeDataLayerEditing();
}

bool UDataLayerInstance::CanParent(const UDataLayerInstance* InParent) const
{
	return (this != InParent) && (Parent != InParent) && IsRuntime() == InParent->IsRuntime();
}

void UDataLayerInstance::SetParent(UDataLayerInstance* InParent)
{
	if (!CanParent(InParent))
	{
		return;
	}

	Modify();
	if (Parent)
	{
		Parent->RemoveChild(this);
	}
	Parent = InParent;
	if (Parent)
	{
		Parent->AddChild(this);
	}
}

void UDataLayerInstance::SetChildParent(UDataLayerInstance* InParent)
{
	if (this == InParent)
	{
		return;
	}

	Modify();
	while (Children.Num())
	{
		Children[0]->SetParent(InParent);
	};
}

void UDataLayerInstance::RemoveChild(UDataLayerInstance* InDataLayer)
{
	Modify();
	check(Children.Contains(InDataLayer));
	Children.RemoveSingle(InDataLayer);
}

void UDataLayerInstance::CheckForErrors() const
{
	auto CheckRuntime = [this](UDataLayerInstance const* Child)
	{
		if (Child->IsRuntime() != IsRuntime())
		{
			TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Error()
				->AddToken(FTextToken::Create(LOCTEXT("MapCheck_DataLayers_DataLayer", "Data layer")))
				->AddToken(FUObjectToken::Create(this))
				->AddToken(FTextToken::Create(LOCTEXT("MapCheck_DataLayers_Asset", "asset")))
				->AddToken(FAssetNameToken::Create(GetAsset()->GetPathName()))
				->AddToken(FTextToken::Create(LOCTEXT("MapCheck_DataLayers_is runtime", "is Runtime but its child data layer")))
				->AddToken(FUObjectToken::Create(Child))
				->AddToken(FTextToken::Create(LOCTEXT("MapCheck_DataLayers_Asset", "asset")))
				->AddToken(FAssetNameToken::Create(Child->GetAsset()->GetPathName()))
				->AddToken(FTextToken::Create(LOCTEXT("MapCheck_DataLayers_isnot", "is not")))
				->AddToken(FMapErrorToken::Create(FName(TEXT("DataLayers_CheckForErrors"))));
		}

		return true;
	};

	ForEachChild(CheckRuntime);
	ForEachChild([](UDataLayerInstance const* Child) { Child->CheckForErrors(); return true; });
}

#endif

bool UDataLayerInstance::IsInitiallyVisible() const
{
#if WITH_EDITOR
	return bIsInitiallyVisible;
#else
	return false;
#endif
}

bool UDataLayerInstance::IsVisible() const
{
#if WITH_EDITOR
	return bIsVisible;
#else
	return false;
#endif
}

bool UDataLayerInstance::IsEffectiveVisible() const
{
#if WITH_EDITOR
	bool bResult = IsVisible();
	const UDataLayerInstance* ParentDataLayer = GetParent();
	while (ParentDataLayer && bResult)
	{
		bResult = bResult && ParentDataLayer->IsVisible();
		ParentDataLayer = ParentDataLayer->GetParent();
	}
	return bResult && IsEffectiveLoadedInEditor();
#else
	return false;
#endif
}

void UDataLayerInstance::ForEachChild(TFunctionRef<bool(const UDataLayerInstance*)> Operation) const
{
	for (const UDataLayerInstance* Child : Children)
	{
		if (!Operation(Child))
		{
			break;
		}
	}
}

void UDataLayerInstance::AddChild(UDataLayerInstance* InDataLayer)
{
	Modify();
	checkSlow(!Children.Contains(InDataLayer));
	check(InDataLayer->IsRuntime() == IsRuntime());
	Children.Add(InDataLayer);
}

#undef LOCTEXT_NAMESPACE