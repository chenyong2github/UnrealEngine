// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraBakerSettingsDetails.h"
#include "NiagaraBakerRenderer.h"
#include "NiagaraBakerSettings.h"
#include "NiagaraSystem.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"
#include "DetailWidgetRow.h"
#include "ScopedTransaction.h"
#include "SGraphActionMenu.h"

#define LOCTEXT_NAMESPACE "NiagaraBakerSettingsDetails"

void FNiagaraBakerTextureSourceDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MaxDesiredWidth(500.0f)
	[
		SNew(SComboButton)
		.OnGetMenuContent(this, &FNiagaraBakerTextureSourceDetails::OnGetMenuContent)
		.ContentPadding(1)
		.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
		.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(this, &FNiagaraBakerTextureSourceDetails::GetText)
		]
	];
}

FText FNiagaraBakerTextureSourceDetails::GetText() const
{
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	if ( Objects.Num() == 1 )
	{
		FNiagaraBakerTextureSource* TargetVariable = (FNiagaraBakerTextureSource*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);
		return FText::FromString(*TargetVariable->SourceName.ToString());
	}

	return LOCTEXT("Error", "Error");
}

TSharedRef<SWidget> FNiagaraBakerTextureSourceDetails::OnGetMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder;

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(this, &FNiagaraBakerTextureSourceDetails::OnActionSelected)
				.OnCreateWidgetForAction(this, &FNiagaraBakerTextureSourceDetails::OnCreateWidgetForAction)
				.OnCollectAllActions(this, &FNiagaraBakerTextureSourceDetails::CollectAllActions)
				.AutoExpandActionMenu(false)
				.ShowFilterTextBox(true)
			]
		];
}

void FNiagaraBakerTextureSourceDetails::CollectAllActions(FGraphActionListBuilderBase& OutAllActions) const
{
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	if (Objects.Num() == 1)
	{
		if (UNiagaraSystem* NiagaraSystem = Objects[0]->GetTypedOuter<UNiagaraSystem>())
		{
			TArray<FName> RendererOptions = FNiagaraBakerRenderer::GatherAllRenderOptions(NiagaraSystem);
			for ( FName OptionName : RendererOptions )
			{
				FName SourceName;
				auto RendererType = FNiagaraBakerRenderer::GetRenderType(OptionName, SourceName);
				FText CategoryText;

				switch ( RendererType )
				{
					case FNiagaraBakerRenderer::ERenderType::View:
						CategoryText = LOCTEXT("BufferVis", "Buffer Visualization");
						break;
					case FNiagaraBakerRenderer::ERenderType::DataInterface:
						CategoryText = LOCTEXT("BufferVis", "Emitter DataInterface");
						break;
					case FNiagaraBakerRenderer::ERenderType::Particle:
						CategoryText = LOCTEXT("BufferVis", "Particle Attribute");
						break;
				}
				FText MenuText = FText::FromString(*OptionName.ToString());
				TSharedPtr<FNiagaraBakerTextureSourceAction> NewNodeAction(
					new FNiagaraBakerTextureSourceAction(OptionName, CategoryText, MenuText, FText(), 0, FText())
				);
				OutAllActions.AddAction(NewNodeAction);
			}
		}
	}
}

TSharedRef<SWidget> FNiagaraBakerTextureSourceDetails::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData) const
{
	return
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(InCreateData->Action->GetMenuDescription())
		];
}

void FNiagaraBakerTextureSourceDetails::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType) const
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (auto& CurrentAction : SelectedActions)
		{
			if (!CurrentAction.IsValid())
			{
				continue;
			}

			FSlateApplication::Get().DismissAllMenus();
			FNiagaraBakerTextureSourceAction* EventSourceAction = (FNiagaraBakerTextureSourceAction*)CurrentAction.Get();

			FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeBinding", " Change binding to \"{0}\" "), FText::FromName(*EventSourceAction->BindingName.ToString())));
			TArray<UObject*> Objects;
			PropertyHandle->GetOuterObjects(Objects);
			for (UObject* Obj : Objects)
			{
				Obj->Modify();
			}

			PropertyHandle->NotifyPreChange();
			FNiagaraBakerTextureSource* TargetVariable = (FNiagaraBakerTextureSource*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);
			TargetVariable->SourceName = EventSourceAction->BindingName;
			PropertyHandle->NotifyPostChange();
			PropertyHandle->NotifyFinishedChangingProperties();
		}
	}
}

#undef LOCTEXT_NAMESPACE