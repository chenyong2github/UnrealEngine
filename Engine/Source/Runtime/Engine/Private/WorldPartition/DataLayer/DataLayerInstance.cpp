// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerInstance.h"

#include "UObject/UnrealType.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/DataLayer/DataLayerUtils.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationErrorHandler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataLayerInstance)


#define LOCTEXT_NAMESPACE "DataLayer"

UDataLayerInstance::UDataLayerInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, bIsVisible(true)
	, bIsInitiallyVisible(true)
	, bIsInitiallyLoadedInEditor(true)
	, bIsLoadedInEditor(true)
	, bIsLocked(false)
#endif
	, InitialRuntimeState(EDataLayerRuntimeState::Unloaded)
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

UWorld* UDataLayerInstance::GetOuterWorld() const
{
	return UObject::GetTypedOuter<UWorld>();
}

AWorldDataLayers* UDataLayerInstance::GetOuterWorldDataLayers() const
{
	return GetOuterWorld()->GetWorldDataLayers();
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

bool UDataLayerInstance::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	if (IsReadOnly())
	{
		return false;
	}

	if (!IsRuntime() && (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UDataLayerInstance, InitialRuntimeState)))
	{
		return false;
	}

	return true;
}

bool UDataLayerInstance::IsLocked() const
{
	if (bIsLocked)
	{
		return true;
	}

	return IsRuntime() && !GetOuterWorldDataLayers()->GetAllowRuntimeDataLayerEditing();
}

bool UDataLayerInstance::IsReadOnly() const
{
	return GetWorld()->IsGameWorld();
}

const TCHAR* UDataLayerInstance::GetDataLayerIconName() const
{
	return FDataLayerUtils::GetDataLayerIconName(GetType());
}

bool UDataLayerInstance::CanAddActor(AActor* InActor) const
{
	return InActor != nullptr && InActor->SupportsDataLayer() && !InActor->ContainsDataLayer(this);
}

bool UDataLayerInstance::AddActor(AActor* InActor) const
{
	if (CanAddActor(InActor))
	{
		return PerformAddActor(InActor);
	}

	return false;
}

bool UDataLayerInstance::CanRemoveActor(AActor* InActor) const
{
	return InActor->GetDataLayerInstances().Contains(this) || InActor->GetDataLayerInstancesForLevel().Contains(this);
}

bool UDataLayerInstance::RemoveActor(AActor* InActor) const
{
	if (CanRemoveActor(InActor))
	{
		return PerformRemoveActor(InActor);
	}

	return false;
}

bool UDataLayerInstance::CanParent(const UDataLayerInstance* InParent) const
{
	return (this != InParent)
		&& (Parent != InParent) 
		&& (InParent == nullptr || (IsDataLayerTypeValidToParent(InParent->GetType()) && (InParent->GetOuterWorldDataLayers() == GetOuterWorldDataLayers())));
}

bool UDataLayerInstance::IsDataLayerTypeValidToParent(EDataLayerType ParentDataLayerType) const
{
	return GetType() != EDataLayerType::Unknown
		&& ParentDataLayerType != EDataLayerType::Unknown
		&& (ParentDataLayerType == EDataLayerType::Editor || GetType() == EDataLayerType::Runtime);
}

bool UDataLayerInstance::SetParent(UDataLayerInstance* InParent)
{
	if (!CanParent(InParent))
	{
		return false;
	}

	Modify();

	// If we find ourself in the parent chain of the provided parent
	UDataLayerInstance* CurrentInstance = InParent;
	while (CurrentInstance)
	{
		if (CurrentInstance == this)
		{
			// Detach the parent from its parent
			InParent->SetParent(nullptr);
			break;
		}
		CurrentInstance = CurrentInstance->GetParent();
	};

	if (Parent)
	{
		Parent->RemoveChild(this);
	}
	Parent = InParent;
	if (Parent)
	{
		Parent->AddChild(this);
	}

	return true;
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

FText UDataLayerInstance::GetDataLayerText(const UDataLayerInstance* InDataLayer)
{
	return InDataLayer ? FText::FromString(InDataLayer->GetDataLayerShortName()) : LOCTEXT("InvalidDataLayerShortName", "<None>");
}

bool UDataLayerInstance::Validate(IStreamingGenerationErrorHandler* ErrorHandler) const
{
	if (GetParent() != nullptr && !IsDataLayerTypeValidToParent(GetParent()->GetType()))
	{
		ErrorHandler->OnDataLayerHierarchyTypeMismatch(this, GetParent());
		return false;
	}

	return true;
}

bool UDataLayerInstance::IsInActorEditorContext() const
{
	return GetOuterWorldDataLayers()->IsInActorEditorContext(this);
}

bool UDataLayerInstance::AddToActorEditorContext()
{
	return GetOuterWorldDataLayers()->AddToActorEditorContext(this);
}

bool UDataLayerInstance::RemoveFromActorEditorContext()
{
	return GetOuterWorldDataLayers()->RemoveFromActorEditorContext(this);
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
	check(InDataLayer->GetOuterWorldDataLayers() == GetOuterWorldDataLayers())
	Modify();
	checkSlow(!Children.Contains(InDataLayer));
	Children.Add(InDataLayer);
}

EDataLayerRuntimeState UDataLayerInstance::GetRuntimeState() const
{
	return GetOuterWorldDataLayers()->GetDataLayerRuntimeStateByName(GetDataLayerFName());
}

EDataLayerRuntimeState UDataLayerInstance::GetEffectiveRuntimeState() const
{
	return GetOuterWorldDataLayers()->GetDataLayerEffectiveRuntimeStateByName(GetDataLayerFName());
}

bool UDataLayerInstance::SetRuntimeState(EDataLayerRuntimeState InState, bool bInIsRecursive) const
{
	if (GetOuterWorldDataLayers()->HasAuthority())
	{
		GetOuterWorldDataLayers()->SetDataLayerRuntimeState(this, InState, bInIsRecursive);
		return true;
	}
	UE_LOG(LogWorldPartition, Error, TEXT("SetDataLayerRuntimeState can only execute on authority"));
	return false;
}

#undef LOCTEXT_NAMESPACE
