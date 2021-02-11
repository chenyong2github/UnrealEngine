// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraFlipbookSettingsDetails.h"
#include "NiagaraFlipbookRenderer.h"
#include "NiagaraFlipbookSettings.h"
#include "NiagaraSystem.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"
#include "DetailWidgetRow.h"
#include "ScopedTransaction.h"
#include "SGraphActionMenu.h"

#define LOCTEXT_NAMESPACE "NiagaraFlipbookSettingsDetails"

void FNiagaraFlipbookTextureSourceDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
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
		.OnGetMenuContent(this, &FNiagaraFlipbookTextureSourceDetails::OnGetMenuContent)
		.ContentPadding(1)
		.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
		.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(this, &FNiagaraFlipbookTextureSourceDetails::GetText)
		]
	];
}

FText FNiagaraFlipbookTextureSourceDetails::GetText() const
{
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	if ( Objects.Num() == 1 )
	{
		FNiagaraFlipbookTextureSource* TargetVariable = (FNiagaraFlipbookTextureSource*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);
		return FText::FromString(*TargetVariable->SourceName.ToString());
	}

	return LOCTEXT("Error", "Error");
}

TSharedRef<SWidget> FNiagaraFlipbookTextureSourceDetails::OnGetMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder;

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(this, &FNiagaraFlipbookTextureSourceDetails::OnActionSelected)
				.OnCreateWidgetForAction(this, &FNiagaraFlipbookTextureSourceDetails::OnCreateWidgetForAction)
				.OnCollectAllActions(this, &FNiagaraFlipbookTextureSourceDetails::CollectAllActions)
				.AutoExpandActionMenu(false)
				.ShowFilterTextBox(true)
			]
		];
}

void FNiagaraFlipbookTextureSourceDetails::CollectAllActions(FGraphActionListBuilderBase& OutAllActions) const
{
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	if (Objects.Num() == 1)
	{
		if (UNiagaraSystem* NiagaraSystem = Objects[0]->GetTypedOuter<UNiagaraSystem>())
		{
			TArray<FName> RendererOptions = FNiagaraFlipbookRenderer::GatherAllRenderOptions(NiagaraSystem);
			for ( FName OptionName : RendererOptions )
			{
				FName SourceName;
				auto RendererType = FNiagaraFlipbookRenderer::GetRenderType(OptionName, SourceName);
				FText CategoryText;

				switch ( RendererType )
				{
					case FNiagaraFlipbookRenderer::ERenderType::View:
						CategoryText = LOCTEXT("BufferVis", "Buffer Visualization");
						break;
					case FNiagaraFlipbookRenderer::ERenderType::DataInterface:
						CategoryText = LOCTEXT("BufferVis", "Emitter DataInterface");
						break;
					case FNiagaraFlipbookRenderer::ERenderType::Particle:
						CategoryText = LOCTEXT("BufferVis", "Particle Attribute");
						break;
				}
				FText MenuText = FText::FromString(*OptionName.ToString());
				TSharedPtr<FNiagaraFlipbookTextureSourceAction> NewNodeAction(
					new FNiagaraFlipbookTextureSourceAction(OptionName, CategoryText, MenuText, FText(), 0, FText())
				);
				OutAllActions.AddAction(NewNodeAction);
			}
		}
	}
}

TSharedRef<SWidget> FNiagaraFlipbookTextureSourceDetails::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData) const
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

void FNiagaraFlipbookTextureSourceDetails::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType) const
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
			FNiagaraFlipbookTextureSourceAction* EventSourceAction = (FNiagaraFlipbookTextureSourceAction*)CurrentAction.Get();

			FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeBinding", " Change binding to \"{0}\" "), FText::FromName(*EventSourceAction->BindingName.ToString())));
			TArray<UObject*> Objects;
			PropertyHandle->GetOuterObjects(Objects);
			for (UObject* Obj : Objects)
			{
				Obj->Modify();
			}

			PropertyHandle->NotifyPreChange();
			FNiagaraFlipbookTextureSource* TargetVariable = (FNiagaraFlipbookTextureSource*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);
			TargetVariable->SourceName = EventSourceAction->BindingName;
			PropertyHandle->NotifyPostChange();
			PropertyHandle->NotifyFinishedChangingProperties();
		}
	}
}

#undef LOCTEXT_NAMESPACE