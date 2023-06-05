// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/SDMXPixelMappingDMXLibraryView.h"

#include "DMXPixelMapping.h"
#include "DMXPixelMappingComponentReference.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Customizations/DMXPixelMappingDetailCustomization_FixtureGroup.h"
#include "Library/DMXLibrary.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "ViewModels/DMXPixelMappingDMXLibraryViewModel.h"
#include "Widgets/SDMXPixelMappingFixturePatchList.h"

#include "Editor.h"
#include "IDetailsView.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"


#define LOCTEXT_NAMESPACE "SDMXPixelMappingDMXLibraryView"

void SDMXPixelMappingDMXLibraryView::Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit)
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
			.Padding(4.f)
			.AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.ForegroundColor(FLinearColor::White)
				.ToolTipText(LOCTEXT("AddFixtureGroupTooltip", "Adds a Fixture Group to the Pixel Mapping"))
				.ContentPadding(FMargin(5.0f, 1.0f))
				.OnClicked(this, &SDMXPixelMappingDMXLibraryView::OnAddDMXLibraryButtonClicked)
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
						.Text(LOCTEXT("AddFixtureGroupLabel", "Add DMX Library"))
					]
				]
			]

			// Details views 
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SAssignNew(DMXLibrariesScrollBox, SScrollBox)
				.Orientation(EOrientation::Orient_Vertical)
			]
		]
	];

	ForceRefresh();

	UDMXLibrary::GetOnEntitiesAdded().AddSP(this, &SDMXPixelMappingDMXLibraryView::OnEntitiesAddedOrRemoved);
	UDMXLibrary::GetOnEntitiesRemoved().AddSP(this, &SDMXPixelMappingDMXLibraryView::OnEntitiesAddedOrRemoved);

	Toolkit->GetOnSelectedComponentsChangedDelegate().AddSP(this, &SDMXPixelMappingDMXLibraryView::OnComponentSelected);
}

void SDMXPixelMappingDMXLibraryView::RequestRefresh()
{
	if (!RefreshTimerHandle.IsValid())
	{
		RefreshTimerHandle = GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &SDMXPixelMappingDMXLibraryView::ForceRefresh));
	}
}

void SDMXPixelMappingDMXLibraryView::AddReferencedObjects(FReferenceCollector& Collector) 
{
	Collector.AddReferencedObjects(ViewModels);
}

void SDMXPixelMappingDMXLibraryView::ForceRefresh()
{
	RefreshTimerHandle.Invalidate();

	if (!WeakToolkit.IsValid())
	{
		return;
	}
	const TSharedRef<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin().ToSharedRef();

	DMXLibrariesScrollBox->ClearChildren();
	ViewModels.Reset();

	const TArray<UDMXPixelMappingFixtureGroupComponent*> FixtureGroupComponents = GetFixtureGroupComponents();
	for (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent : FixtureGroupComponents)
	{
		UDMXPixelMappingDMXLibraryViewModel* ViewModel = UDMXPixelMappingDMXLibraryViewModel::CreateNew(Toolkit, FixtureGroupComponent);
		if (!ViewModel->IsFixtureGroupOrChildSelected())
		{
			// Only display selected models
			continue;
		}
		ViewModels.Add(ViewModel);

		// Listen to dmx library changes
		ViewModel->OnDMXLibraryChanged.AddSP(this, &SDMXPixelMappingDMXLibraryView::RequestRefresh);

		// Create title content
		const TSharedRef<SHorizontalBox> TitleContentBox = SNew(SHorizontalBox);
		TitleContentBox->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(1.f)
			[
				SNew(STextBlock)
				.TextStyle(FAppStyle::Get(), "DetailsView.CategoryTextStyle")
				.Text_Lambda([FixtureGroupComponent]()
					{
						return FixtureGroupComponent ? FText::FromString(FixtureGroupComponent->GetUserFriendlyName()) : FText::GetEmpty();
					})
			];

		// Create body content
		const TSharedRef<SVerticalBox> BodyContent =
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreateDetailsViewForModel(*ViewModel)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SDMXPixelMappingFixturePatchList, Toolkit, ViewModel)
			];

		// Add to scrollbox
		DMXLibrariesScrollBox->AddSlot()
			[
				SNew(SExpandableArea)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryTop"))
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

void SDMXPixelMappingDMXLibraryView::OnEntitiesAddedOrRemoved(UDMXLibrary* DMXLibrary, TArray<UDMXEntity*> Entities)
{
	RequestRefresh();
}

void SDMXPixelMappingDMXLibraryView::OnComponentSelected()
{
	const TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin();
	if (!Toolkit.IsValid())
	{
		return;
	}

	// Refresh the view if there is no view model for the fixture group of a selected component
	const TSet<FDMXPixelMappingComponentReference>& SelectedComponents = Toolkit->GetSelectedComponents();

	UDMXPixelMappingFixtureGroupComponent* ComponentToFocus = nullptr;
	for (const FDMXPixelMappingComponentReference& ComponentReference : SelectedComponents)
	{
		UDMXPixelMappingBaseComponent* Component = ComponentReference.GetComponent();
		do
		{
			TObjectPtr<UDMXPixelMappingDMXLibraryViewModel>* CorrespondingViewModelPtr = Algo::FindByPredicate(ViewModels, [Component](const UDMXPixelMappingDMXLibraryViewModel* ViewModel)
				{
					return ViewModel->GetFixtureGroupComponent() == Component;
				});
			if (!CorrespondingViewModelPtr)
			{
				RequestRefresh();
				return;
			}

			Component = Component->GetParent();
		} while (Component);
	}
}

FReply SDMXPixelMappingDMXLibraryView::OnAddDMXLibraryButtonClicked()
{
	if (!WeakToolkit.IsValid())
	{
		return FReply::Unhandled();
	}
	const TSharedRef<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin().ToSharedRef();

	UDMXPixelMappingRootComponent* RootComponent = GetPixelMappingRootComponent();
	UDMXPixelMappingRendererComponent* ActiveRendererComponent = Toolkit->GetActiveRendererComponent();
	if (!RootComponent || !ActiveRendererComponent)
	{
		return FReply::Handled();
	}

	const TArray<UDMXPixelMappingFixtureGroupComponent*> OtherFixtureGroupComponents = GetFixtureGroupComponents();

	const FScopedTransaction AddFixtureGroupTransaction(LOCTEXT("AddFixtureGroupTransaction", "Add Fixture Group"));
	const TSharedRef<FDMXPixelMappingComponentTemplate> Template = MakeShared<FDMXPixelMappingComponentTemplate>(UDMXPixelMappingFixtureGroupComponent::StaticClass());
	const TArray<UDMXPixelMappingBaseComponent*> NewComponents = Toolkit->CreateComponentsFromTemplates(RootComponent, ActiveRendererComponent, TArray<TSharedPtr<FDMXPixelMappingComponentTemplate>>{ Template });
	if (!ensureMsgf(NewComponents.Num() == 1 && NewComponents[0]->GetClass() == UDMXPixelMappingFixtureGroupComponent::StaticClass(), TEXT("Cannot find newly added Fixture Group Component, hence cannot select it.")))
	{
		return FReply::Handled();
	}
	UDMXPixelMappingFixtureGroupComponent* NewFixtureGroupComponent = CastChecked<UDMXPixelMappingFixtureGroupComponent>(NewComponents[0]);
	SelectFixtureGroupComponent(NewFixtureGroupComponent);

	// Place the new fixture group
	if (OtherFixtureGroupComponents.IsEmpty())
	{
		// If there's no group, add one that scales the current texture to the active renderer component
		if (Toolkit->CanSizeSelectedComponentToTexture())
		{
			Toolkit->SizeSelectedComponentToTexture(true);
		}
	}
	else
	{
		// If there's already a group, place it over the bottom right most existing group
		FVector2D NewPosition(0.f, 0.f);
		FVector2D NewSize = NewFixtureGroupComponent->GetSize();
		for (UDMXPixelMappingFixtureGroupComponent* Other : OtherFixtureGroupComponents)
		{
			if (Other->GetPosition().Y > NewPosition.Y)
			{
				NewPosition = Other->GetPosition();
				NewSize = Other->GetSize();
			}
		}
		NewPosition += FVector2D(FMath::Max(1.f, NewSize.X / 16), FMath::Max(1.f, NewSize.Y / 16));

		NewFixtureGroupComponent->SetPosition(NewPosition);
		NewFixtureGroupComponent->SetSize(NewSize);
	}

	return FReply::Handled();
}

TSharedRef<SWidget> SDMXPixelMappingDMXLibraryView::CreateDetailsViewForModel(UDMXPixelMappingDMXLibraryViewModel& Model) const
{
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	const TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	DetailsView->SetObject(&Model);

	return DetailsView;
}

void SDMXPixelMappingDMXLibraryView::SelectFixtureGroupComponent(UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent)
{
	if (const TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin())
	{
		const FDMXPixelMappingComponentReference ComponentRefToSelect(Toolkit, FixtureGroupComponent);
		const TSet<FDMXPixelMappingComponentReference> NewSelection{ ComponentRefToSelect };
		Toolkit->SelectComponents(NewSelection);
	}
}

UDMXPixelMappingRootComponent* SDMXPixelMappingDMXLibraryView::GetPixelMappingRootComponent() const
{
	const TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.IsValid() ? WeakToolkit.Pin() : nullptr;
	UDMXPixelMapping* PixelMapping = Toolkit.IsValid() ? Toolkit->GetDMXPixelMapping() : nullptr;

	return PixelMapping ? PixelMapping->GetRootComponent() : nullptr;
}

TArray<UDMXPixelMappingFixtureGroupComponent*> SDMXPixelMappingDMXLibraryView::GetFixtureGroupComponents() const
{
	TArray<UDMXPixelMappingFixtureGroupComponent*> FixtureGroupComponents;
	UDMXPixelMappingRendererComponent* RendererComponent = WeakToolkit.IsValid() ? WeakToolkit.Pin()->GetActiveRendererComponent() : nullptr;
	if (RendererComponent)
	{
		for (UDMXPixelMappingBaseComponent* Child : RendererComponent->GetChildren())
		{
			if (UDMXPixelMappingFixtureGroupComponent* GroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Child))
			{
				FixtureGroupComponents.Add(GroupComponent);
			}
		}
	}

	return FixtureGroupComponents;
}

#undef LOCTEXT_NAMESPACE
