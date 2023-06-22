// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/SDMXPixelMappingInputSourceView.h"

#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Customizations/DMXPixelMappingDetailCustomization_Renderer.h"
#include "Customizations/DMXPixelMappingPreprocessRendererDetails.h"
#include "DMXPixelMapping.h"
#include "DMXPixelMappingComponentReference.h"
#include "DMXPixelMappingPreprocessRenderer.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "Views/SDMXPixelMappingDesignerView.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXPixelMappingInputSourceView"

void SDMXPixelMappingInputSourceView::Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit)
{
	WeakToolkit = InToolkit;
	if (!WeakToolkit.IsValid())
	{
		return;
	}
	const TSharedRef<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin().ToSharedRef();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			// Add button
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Fill)
			.Padding(4.f)
			.AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.ForegroundColor(FLinearColor::White)
				.ToolTipText(LOCTEXT("AddInputSourceTooltip", "Adds a new input to the pixelmapping asset"))
				.ContentPadding(FMargin(5.0f, 1.0f))
				.OnClicked(this, &SDMXPixelMappingInputSourceView::OnAddButtonClicked)
				.Content()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(0.f, 1.f))
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Plus"))
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(FMargin(2.f, 0.f, 2.f, 0.f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AddInputSourceLabel", "Add"))
					]
				]
			]

			// Details views 
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.FillHeight(1.f)
			[
				SAssignNew(DetailsViewsScrollBox, SScrollBox)
				.Orientation(EOrientation::Orient_Vertical)
			]
		]
	];

	ForceRefresh();

	UDMXPixelMappingBaseComponent::GetOnComponentAdded().AddSP(this, &SDMXPixelMappingInputSourceView::OnComponentAddedOrRemoved);
	UDMXPixelMappingBaseComponent::GetOnComponentRemoved().AddSP(this, &SDMXPixelMappingInputSourceView::OnComponentAddedOrRemoved);
	Toolkit->GetOnSelectedComponentsChangedDelegate().AddSP(this, &SDMXPixelMappingInputSourceView::OnComponentSelected);
}

void SDMXPixelMappingInputSourceView::RequestRefresh()
{
	if (!RefreshTimerHandle.IsValid())
	{
		RefreshTimerHandle = GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &SDMXPixelMappingInputSourceView::ForceRefresh));
	}
}

void SDMXPixelMappingInputSourceView::ForceRefresh()
{
	RefreshTimerHandle.Invalidate();

	if (!WeakToolkit.IsValid())
	{
		return;
	}
	const TSharedRef<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin().ToSharedRef();

	DetailsViewsScrollBox->ClearChildren();
	const TArray<UDMXPixelMappingRendererComponent*> RendererComponents = GetRendererComponents();
	NumRendererComponents = RendererComponents.Num();
	for (UDMXPixelMappingRendererComponent* RendererComponent : RendererComponents)
	{
		const TSharedRef<SVerticalBox> BodyContent = SNew(SVerticalBox);
		
		// Create a title view for each renderer component
		const TSharedRef<SHorizontalBox> TitleContentBox = SNew(SHorizontalBox);
			
		TitleContentBox->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(1.f)
			[
				SNew(STextBlock)
				.TextStyle(FAppStyle::Get(), "DetailsView.CategoryTextStyle")
				.Text_Lambda([RendererComponent]()
					{
						return RendererComponent ? FText::FromString(RendererComponent->GetUserFriendlyName()) : FText::GetEmpty();
					})
			];

		TitleContentBox->AddSlot()
			.Padding(2.f, 0.f)
			[
				SNew(SSeparator)
				.Orientation(EOrientation::Orient_Horizontal)
			];

		if (RendererComponents.Num() > 1)
		{
			TitleContentBox->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(4.f)
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.ForegroundColor(FLinearColor::White)
					.ToolTipText(LOCTEXT("SelectRendererComponentTooltip", "Selects the input source"))
					.ContentPadding(FMargin(5.0f, 1.0f))
					.OnClicked(this, &SDMXPixelMappingInputSourceView::OnSelectRendererComponentButtonClicked, RendererComponent)
					.Content()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SelectRendererComponentLabel", "Select"))
					]
				];
		}

		// Create a details view for the active renderer component
		if (Toolkit->GetActiveRendererComponent() == RendererComponent)
		{
			FDetailsViewArgs DetailsViewArgs;
			DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
			DetailsViewArgs.bHideSelectionTip = true;
			DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;

			FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			const TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

			FOnGetDetailCustomizationInstance RendererCustomizationInstance = FOnGetDetailCustomizationInstance::CreateStatic(&FDMXPixelMappingDetailCustomization_Renderer::MakeInstance, WeakToolkit);
			DetailsView->RegisterInstancedCustomPropertyLayout(UDMXPixelMappingRendererComponent::StaticClass(), RendererCustomizationInstance);

			using namespace UE::DMXPixelMapping::Customizations;
			FOnGetDetailCustomizationInstance RenderInputTextureProxyCustomizationInstance = FOnGetDetailCustomizationInstance::CreateStatic(&FDMXPixelMappingPreprocessRendererDetails::MakeInstance);
			DetailsView->RegisterInstancedCustomPropertyLayout(UDMXPixelMappingPreprocessRenderer::StaticClass(), RenderInputTextureProxyCustomizationInstance);
			
			DetailsView->SetObject(RendererComponent);

			BodyContent->AddSlot()
				.AutoHeight()
				[
					DetailsView
				];
		}

		// Add to scrollbox
		DetailsViewsScrollBox->AddSlot()
			[
				SNew(SExpandableArea)
				.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
				.BodyBorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))				
				.HeaderPadding(4.f)
				.Padding(FMargin(8.f, 2.f, 2.f, 0.f))
				.HeaderContent()
				[
					TitleContentBox
				]
				.BodyContent()
				[
					BodyContent
				]
			];
	}
}

void SDMXPixelMappingInputSourceView::OnComponentSelected()
{
	if (const TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin())
	{
		// Refresh the view, only if a renderer component is selected
		const FDMXPixelMappingComponentReference* const SelectedRendererComponentRefPtr = Algo::FindByPredicate(Toolkit->GetSelectedComponents(), [](const FDMXPixelMappingComponentReference& ComponentRef)
			{
				const UDMXPixelMappingBaseComponent* Component = ComponentRef.GetComponent();
				return Component->GetClass() == UDMXPixelMappingRendererComponent::StaticClass();
			});
		if (SelectedRendererComponentRefPtr)
		{
			RequestRefresh();
		}
	}
}

void SDMXPixelMappingInputSourceView::OnComponentAddedOrRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component)
{
	RequestRefresh();
}

FReply SDMXPixelMappingInputSourceView::OnAddButtonClicked()
{
	const TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin();
	UDMXPixelMapping* PixelMapping = Toolkit.IsValid() ? Toolkit->GetDMXPixelMapping() : nullptr;
	if (Toolkit.IsValid() && PixelMapping && PixelMapping->GetRootComponent())
	{
		const FScopedTransaction AddRendererTransaction(LOCTEXT("AddRendererTransaction", "Add Pixel Mapping Input Source"));
		PixelMapping->GetRootComponent()->PreEditChange(nullptr);
		Toolkit->AddRenderer();
		PixelMapping->GetRootComponent()->PostEditChange();
		
		UDMXPixelMappingRendererComponent* RendererComponentToSelect = Toolkit->GetActiveRendererComponent();
		if (RendererComponentToSelect)
		{
			const FDMXPixelMappingComponentReference ComponentRefToSelect(Toolkit, RendererComponentToSelect);
			const TSet<FDMXPixelMappingComponentReference> NewSelection{ ComponentRefToSelect };
			Toolkit->SelectComponents(NewSelection);

			const TSharedRef<SDMXPixelMappingDesignerView> DesignerView = Toolkit->GetOrCreateDesignerView();
			constexpr bool bInstantZoom = false;
			DesignerView->ZoomToFit(bInstantZoom);
		}

		RequestRefresh();
		DetailsViewsScrollBox->ScrollToEnd();
	}

	return FReply::Handled();
}

FReply SDMXPixelMappingInputSourceView::OnSelectRendererComponentButtonClicked(UDMXPixelMappingRendererComponent* RendererComponent)
{
	const TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin();
	if (Toolkit.IsValid() && RendererComponent)
	{
		const FDMXPixelMappingComponentReference ComponentRefToSelect(Toolkit, RendererComponent);
		const TSet<FDMXPixelMappingComponentReference> NewSelection{ ComponentRefToSelect };
		Toolkit->SelectComponents(NewSelection);
	}

	return FReply::Handled();
}

TArray<UDMXPixelMappingRendererComponent*> SDMXPixelMappingInputSourceView::GetRendererComponents() const
{
	TArray<UDMXPixelMappingRendererComponent*> Result;
	if (!WeakToolkit.IsValid())
	{
		return Result;
	}
	const TSharedRef<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin().ToSharedRef();

	UDMXPixelMappingRootComponent* RootComponent = Toolkit->GetDMXPixelMapping() ? Toolkit->GetDMXPixelMapping()->GetRootComponent() : nullptr;
	if (!RootComponent)
	{
		return Result;
	}

	for (UDMXPixelMappingBaseComponent* Child : RootComponent->GetChildren())
	{
		if (UDMXPixelMappingRendererComponent* RendererComponent = Cast<UDMXPixelMappingRendererComponent>(Child))
		{
			Result.Add(RendererComponent);
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
