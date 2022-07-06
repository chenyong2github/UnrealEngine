// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRenderPagesPropsRemoteControl.h"
#include "UI/Components/SRenderPagesRemoteControlTreeNode.h"
#include "UI/Components/SRenderPagesRemoteControlField.h"
#include "IRenderPageCollectionEditor.h"
#include "IRenderPagesModule.h"
#include "RenderPage/RenderPageCollection.h"
#include "RenderPage/RenderPageManager.h"
#include "SlateOptMacros.h"

#define LOCTEXT_NAMESPACE "SRenderPagesPropsRemoteControl"


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPropsRemoteControl::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor, URenderPagePropsSourceRemoteControl* InPropsSource)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;
	PropsSource = InPropsSource;

	SAssignNew(RowWidgetsContainer, SVerticalBox);
	UpdateStoredValuesAndRefresh();

	InBlueprintEditor->OnRenderPagesSelectionChanged().AddSP(this, &SRenderPagesPropsRemoteControl::Refresh);
	if (IsValid(PropsSource))
	{
		if (TObjectPtr<URemoteControlPreset> Preset = PropsSource->GetProps()->GetRemoteControlPreset())
		{
			Preset->OnEntityExposed().AddSP(this, &SRenderPagesPropsRemoteControl::OnRemoteControlEntitiesExposed);
			Preset->OnEntityUnexposed().AddSP(this, &SRenderPagesPropsRemoteControl::OnRemoteControlEntitiesUnexposed);
			Preset->OnEntitiesUpdated().AddSP(this, &SRenderPagesPropsRemoteControl::OnRemoteControlEntitiesUpdated);
			Preset->OnExposedPropertiesModified().AddSP(this, &SRenderPagesPropsRemoteControl::OnRemoteControlExposedPropertiesModified);
		}
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(8.0f)
		.BorderImage(new FSlateNoResource())
		[
			RowWidgetsContainer.ToSharedRef()
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION


void UE::RenderPages::Private::SRenderPagesPropsRemoteControl::UpdateStoredValuesAndRefresh()
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		IRenderPagesModule::Get().GetManager().UpdatePagesPropValues(BlueprintEditor->GetInstance());
		Refresh();
	}
}

void UE::RenderPages::Private::SRenderPagesPropsRemoteControl::Refresh()
{
	if (!RowWidgetsContainer.IsValid())
	{
		return;
	}

	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		TArray<FRenderPagesRemoteControlGenerateWidgetArgs> NewRowWidgetsArgs;
		if (IsValid(PropsSource))
		{
			URenderPagePropsRemoteControl* Props = PropsSource->GetProps();
			for (URenderPagePropRemoteControl* Prop : Props->GetAllCasted())
			{
				TArray<uint8> PropData;
				if (!GetSelectedPageFieldValue(Prop->GetRemoteControlEntity(), PropData))
				{
					continue;
				}

				if (!BlueprintEditor->IsCurrentlyRenderingOrPlaying())
				{
					if (!Prop->SetValue(PropData))
					{
						continue;
					}
				}
				else if (!Prop->CanSetValue(PropData))
				{
					continue;
				}

				if (URemoteControlPreset* Preset = Props->GetRemoteControlPreset(); IsValid(Preset))
				{
					FRenderPagesRemoteControlGenerateWidgetArgs Args;
					Args.Preset = Preset;
					Args.Entity = Prop->GetRemoteControlEntity();
					Args.ColumnSizeData.LeftColumnWidth = 0.3;
					Args.ColumnSizeData.RightColumnWidth = 0.7;
					NewRowWidgetsArgs.Add(Args);
				}
			}
		}

		if (RowWidgetsArgs != NewRowWidgetsArgs)
		{
			RowWidgetsArgs = NewRowWidgetsArgs;
			RowWidgetsContainer->ClearChildren();
			RowWidgets.Empty();
			for (const FRenderPagesRemoteControlGenerateWidgetArgs& RowWidgetArgs : RowWidgetsArgs)
			{
				TSharedPtr<SRenderPagesRemoteControlTreeNode> RowWidget = SRenderPagesRemoteControlField::MakeInstance(RowWidgetArgs);
				RowWidgets.Add(RowWidget);
				RowWidgetsContainer->AddSlot()
					.Padding(0.0f)
					.AutoHeight()
					[
						RowWidget.ToSharedRef()
					];
			}
		}
		else
		{
			for (const TSharedPtr<SRenderPagesRemoteControlTreeNode>& RowWidget : RowWidgets)
			{
				RowWidget->RefreshValue();
			}
		}
	}
}


void UE::RenderPages::Private::SRenderPagesPropsRemoteControl::OnRemoteControlExposedPropertiesModified(URemoteControlPreset* Preset, const TSet<FGuid>& ModifiedProperties)
{
	if (!IsValid(Preset))
	{
		return;
	}
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (BlueprintEditor->IsCurrentlyRenderingOrPlaying())
		{
			return;
		}

		bool bModified = false;
		for (const FGuid& Id : ModifiedProperties)
		{
			if (const TSharedPtr<FRemoteControlEntity> Entity = Preset->GetExposedEntity<FRemoteControlEntity>(Id).Pin())
			{
				TArray<uint8> BinaryArray;
				if (!URenderPagePropRemoteControl::GetValueOfEntity(Entity, BinaryArray))
				{
					continue;
				}
				TArray<uint8> StoredBinaryArray;
				if (!GetSelectedPageFieldValue(Entity, StoredBinaryArray))
				{
					continue;
				}
				if (BinaryArray == StoredBinaryArray)
				{
					continue;
				}

				if (!SetSelectedPageFieldValue(Entity, BinaryArray))
				{
					continue;
				}
				bModified = true;
			}
		}

		if (bModified)
		{
			BlueprintEditor->MarkAsModified();
			BlueprintEditor->OnRenderPagesChanged().Broadcast();
			Refresh();
		}
	}
}


URenderPage* UE::RenderPages::Private::SRenderPagesPropsRemoteControl::GetSelectedPage()
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (const TArray<URenderPage*> SelectedPages = BlueprintEditor->GetSelectedRenderPages(); (SelectedPages.Num() == 1))
		{
			return SelectedPages[0];
		}
	}
	return nullptr;
}

bool UE::RenderPages::Private::SRenderPagesPropsRemoteControl::GetSelectedPageFieldValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, TArray<uint8>& OutBinaryArray)
{
	OutBinaryArray.Empty();
	if (URenderPage* Page = GetSelectedPage(); IsValid(Page))
	{
		return Page->GetRemoteControlValue(RemoteControlEntity, OutBinaryArray);
	}
	return false;
}

bool UE::RenderPages::Private::SRenderPagesPropsRemoteControl::SetSelectedPageFieldValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, const TArray<uint8>& BinaryArray)
{
	if (URenderPage* Page = GetSelectedPage(); IsValid(Page))
	{
		return Page->SetRemoteControlValue(RemoteControlEntity, BinaryArray);
	}
	return false;
}


#undef LOCTEXT_NAMESPACE
