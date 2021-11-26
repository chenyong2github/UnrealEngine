// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DataLayer.cpp: UDataLayer class implementation
=============================================================================*/

#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"

#define LOCTEXT_NAMESPACE "DataLayer"

UDataLayer::UDataLayer(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
, bIsInitiallyActive_DEPRECATED(false)
, bIsVisible(true)
, bIsInitiallyVisible(true)
, bIsInitiallyLoadedInEditor(true)
, bIsLoadedInEditor(true)
, bIsLocked(false)
#endif
, DataLayerLabel(GetFName())
, bIsRuntime(false)
, InitialRuntimeState(EDataLayerRuntimeState::Unloaded)
, DebugColor(FColor::Black)
{
}

void UDataLayer::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (bIsInitiallyActive_DEPRECATED)
	{
		InitialRuntimeState = EDataLayerRuntimeState::Activated;
	}

	// Initialize bIsVisible with persistent flag bIsInitiallyVisible
	bIsVisible = bIsInitiallyVisible;

	// Sanitize Label
	DataLayerLabel = UDataLayer::GetSanitizedDataLayerLabel(DataLayerLabel);

	if (DebugColor == FColor::Black)
	{
		FRandomStream RandomStream(GetFName());
		const uint8 R = (uint8)(RandomStream.GetFraction() * 255.f);
		const uint8 G = (uint8)(RandomStream.GetFraction() * 255.f);
		const uint8 B = (uint8)(RandomStream.GetFraction() * 255.f);
		DebugColor = FColor(R, G, B);
	}
#endif

	if (Parent)
	{
		Parent->AddChild(this);
	}
}

bool UDataLayer::IsInitiallyVisible() const
{
#if WITH_EDITOR
	return bIsInitiallyVisible;
#else
	return false;
#endif
}

bool UDataLayer::IsVisible() const
{
#if WITH_EDITOR
	return bIsVisible;
#else
	return false;
#endif
}

FName UDataLayer::GetSanitizedDataLayerLabel(FName InDataLayerLabel)
{
	// Removes all quotes as well as whitespace characters from the startand end
	return FName(InDataLayerLabel.ToString().TrimStartAndEnd().Replace(TEXT("\""), TEXT("")));
}

bool UDataLayer::IsEffectiveVisible() const
{
#if WITH_EDITOR
	bool bResult = IsVisible();
	const UDataLayer* ParentDataLayer = GetParent();
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

void UDataLayer::AddChild(UDataLayer* InDataLayer)
{
	Modify();
	checkSlow(!Children.Contains(InDataLayer));
	Children.Add(InDataLayer);
#if WITH_EDITOR
	if (IsRuntime())
	{
		InDataLayer->SetIsRuntime(true);
	}
#endif
}

#if WITH_EDITOR

bool UDataLayer::IsEffectiveLoadedInEditor() const
{
	bool bResult = IsLoadedInEditor();
	const UDataLayer* ParentDataLayer = GetParent();
	while (ParentDataLayer && bResult)
	{
		bResult = bResult && ParentDataLayer->IsLoadedInEditor();
		ParentDataLayer = ParentDataLayer->GetParent();
	}
	return bResult;
}

bool UDataLayer::IsLocked() const
{
	if (bIsLocked)
	{
		return true;
	}

	return IsRuntime() && !GetOuterAWorldDataLayers()->GetAllowRuntimeDataLayerEditing();
}

bool UDataLayer::CanEditChange(const FProperty* InProperty) const
{
	if ((InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UDataLayer, bIsRuntime)) ||
		(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UDataLayer, InitialRuntimeState)) ||
		(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UDataLayer, DebugColor)))
	{
		if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UDataLayer, bIsRuntime))
		{
			// If DataLayer is Runtime because of its parent being Runtime, we don't allow modifying 
			if (Parent && Parent->IsRuntime())
			{
				check(IsRuntime());
				return false;
			}
		}
		return GetOuterAWorldDataLayers()->GetAllowRuntimeDataLayerEditing();
	}

	return Super::CanEditChange(InProperty);
}

void UDataLayer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName NAME_IsRuntime = GET_MEMBER_NAME_CHECKED(UDataLayer, bIsRuntime);
	FProperty* MemberPropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FName MemberPropertyName = MemberPropertyThatChanged != NULL ? MemberPropertyThatChanged->GetFName() : NAME_None;
	if (MemberPropertyName == NAME_IsRuntime)
	{
		PropagateIsRuntime();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool UDataLayer::CanParent(const UDataLayer* InParent) const
{
	return (this != InParent) && (Parent != InParent);
}

void UDataLayer::SetParent(UDataLayer* InParent)
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

void UDataLayer::SetChildParent(UDataLayer* InParent)
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

void UDataLayer::RemoveChild(UDataLayer* InDataLayer)
{
	Modify();
	check(Children.Contains(InDataLayer));
	Children.RemoveSingle(InDataLayer);
}

const TCHAR* UDataLayer::GetDataLayerIconName() const
{
	return IsRuntime() ? TEXT("DataLayer.Runtime") : TEXT("DataLayer.Editor");
}

void UDataLayer::SetDataLayerLabel(FName InDataLayerLabel)
{
	FName DataLayerLabelSanitized = UDataLayer::GetSanitizedDataLayerLabel(InDataLayerLabel);
	if (DataLayerLabel != DataLayerLabelSanitized)
	{
		Modify();
		AWorldDataLayers* WorldDataLayers = GetOuterAWorldDataLayers();
		check(!WorldDataLayers || !WorldDataLayers->GetDataLayerFromLabel(DataLayerLabelSanitized))
		DataLayerLabel = DataLayerLabelSanitized;
	}
}

void UDataLayer::SetVisible(bool bInIsVisible)
{
	if (bIsVisible != bInIsVisible)
	{
		Modify(/*bAlwaysMarkDirty*/false);
		bIsVisible = bInIsVisible;
	}
}

void UDataLayer::SetIsInitiallyVisible(bool bInIsInitiallyVisible)
{
	if (bIsInitiallyVisible != bInIsInitiallyVisible)
	{
		Modify();
		bIsInitiallyVisible = bInIsInitiallyVisible;
	}
}

void UDataLayer::SetIsRuntime(bool bInIsRuntime)
{
	if (bIsRuntime != bInIsRuntime)
	{
		Modify();
		bIsRuntime = bInIsRuntime;

		PropagateIsRuntime();
	}
}

void UDataLayer::PropagateIsRuntime()
{
	if (IsRuntime())
	{
		for (UDataLayer* Child : Children)
		{
			Child->SetIsRuntime(true);
		}
	}
}

void UDataLayer::SetIsLoadedInEditor(bool bInIsLoadedInEditor, bool bInFromUserChange)
{
	if (bIsLoadedInEditor != bInIsLoadedInEditor)
	{
		Modify(false);
		bIsLoadedInEditor = bInIsLoadedInEditor;
		bIsLoadedInEditorChangedByUserOperation |= bInFromUserChange;
	}
}

FText UDataLayer::GetDataLayerText(const UDataLayer* InDataLayer)
{
	return InDataLayer ? FText::FromName(InDataLayer->GetDataLayerLabel()) : LOCTEXT("InvalidDataLayerLabel", "<None>");
}

#endif

void UDataLayer::ForEachChild(TFunctionRef<bool(const UDataLayer*)> Operation) const
{
	for (UDataLayer* Child : Children)
	{
		if (!Operation(Child))
		{
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE