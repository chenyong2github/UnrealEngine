// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelExposedActor.h"

#include "AssetRegistry/AssetData.h"
#include "PropertyCustomizationHelpers.h"
#include "RemoteControlActor.h"
#include "RemoteControlPreset.h"
#include "ScopedTransaction.h"
#include "SRCPanelDragHandle.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanelActor"

TSharedPtr<SRCPanelTreeNode> SRCPanelExposedActor::MakeInstance(const FGenerateWidgetArgs& Args)
{
	return SNew(SRCPanelExposedActor, StaticCastSharedPtr<FRemoteControlActor>(Args.Entity), Args.Preset, Args.ColumnSizeData).EditMode(Args.bIsInEditMode);
}

void SRCPanelExposedActor::Construct(const FArguments& InArgs, TWeakPtr<FRemoteControlActor> InWeakActor, URemoteControlPreset* InPreset, FRCColumnSizeData InColumnSizeData)
{
	FString ActorPath;

	const TSharedPtr<FRemoteControlActor> Actor = InWeakActor.Pin();
	if (ensure(Actor))
	{
		Initialize(Actor->GetId(), InPreset, InArgs._EditMode);
		
		ColumnSizeData = MoveTemp(InColumnSizeData);
		WeakPreset = InPreset;
		bEditMode = InArgs._EditMode;
		WeakActor = MoveTemp(InWeakActor);
		
		if (AActor* ResolvedActor = Actor->GetActor())
		{
			ActorPath = ResolvedActor->GetPathName();
		}
	}
	
	ChildSlot
	[
		RecreateWidget(MoveTemp(ActorPath))
	];
}

SRCPanelTreeNode::ENodeType SRCPanelExposedActor::GetRCType() const
{
	return ENodeType::Actor;
}

void SRCPanelExposedActor::Refresh()
{
	if (TSharedPtr<FRemoteControlActor> RCActor = WeakActor.Pin())
	{
		CachedLabel = RCActor->GetLabel();

		if (AActor* Actor = RCActor->GetActor())
		{
			ChildSlot.AttachWidget(RecreateWidget(Actor->GetPathName()));
			return;
		}
	}

	ChildSlot.AttachWidget(RecreateWidget(FString()));
}

TSharedRef<SWidget> SRCPanelExposedActor::RecreateWidget(const FString& Path)
{
	TSharedRef<SObjectPropertyEntryBox> EntryBox = SNew(SObjectPropertyEntryBox)
		.ObjectPath(Path)
		.AllowedClass(AActor::StaticClass())
		.OnObjectChanged(this, &SRCPanelExposedActor::OnChangeActor)
		.AllowClear(false)
		.DisplayUseSelected(true)
		.DisplayBrowse(true)
		.NewAssetFactories(TArray<UFactory*>());

	float MinWidth = 0.f, MaxWidth = 0.f;
	EntryBox->GetDesiredWidth(MinWidth, MaxWidth);

	TSharedRef<SWidget> ValueWidget =
		SNew(SBox)
		.MinDesiredWidth(MinWidth)
		.MaxDesiredWidth(MaxWidth)
		[
			MoveTemp(EntryBox)
		];

	return CreateEntityWidget(MoveTemp(ValueWidget));
}

void SRCPanelExposedActor::OnChangeActor(const FAssetData& AssetData)
{
	if (AActor* Actor = Cast<AActor>(AssetData.GetAsset()))
	{
		if (TSharedPtr<FRemoteControlActor> RCActor = WeakActor.Pin())
		{
			FScopedTransaction Transaction(LOCTEXT("ChangeExposedActor", "Change Exposed Actor"));
			RCActor->SetActor(Actor);
			ChildSlot.AttachWidget(RecreateWidget(Actor->GetPathName()));
		}
	}
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPanelActor*/