// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterColorGradingGenerator_RootActor.h"

#include "DisplayClusterColorGradingStyle.h"
#include "IDisplayClusterColorGrading.h"
#include "IDisplayClusterColorGradingDrawerSingleton.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterICVFXCameraComponent.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfigurationTypes_ICVFX.h"
#include "DisplayClusterConfigurationTypes_Postprocess.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailCustomization.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "PropertyHandle.h"
#include "PropertyPathHelpers.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "DisplayClusterColorGrading"

#define CATEGORY_OVERRIDE_NAME(Category) TEXT(Category "_Override")

FDisplayClusterColorGradingDataModel::FColorGradingGroup FDisplayClusterColorGradingGenerator_ColorGradingRenderingSettings::CreateColorGradingGroup(const TSharedPtr<IPropertyHandle>& GroupPropertyHandle)
{
	FDisplayClusterColorGradingDataModel::FColorGradingGroup ColorGradingGroup;
	ColorGradingGroup.DisplayName = GroupPropertyHandle->GetPropertyDisplayName();
	ColorGradingGroup.GroupPropertyHandle = GroupPropertyHandle;

	ColorGradingGroup.ColorGradingElements.Add(CreateColorGradingElement(GroupPropertyHandle, TEXT("Global"), LOCTEXT("ColorGrading_GlobalLabel", "Global")));
	ColorGradingGroup.ColorGradingElements.Add(CreateColorGradingElement(GroupPropertyHandle, TEXT("Shadows"), LOCTEXT("ColorGrading_ShadowsLabel", "Shadows")));
	ColorGradingGroup.ColorGradingElements.Add(CreateColorGradingElement(GroupPropertyHandle, TEXT("Midtones"), LOCTEXT("ColorGrading_MidtonesLabel", "Midtones")));
	ColorGradingGroup.ColorGradingElements.Add(CreateColorGradingElement(GroupPropertyHandle, TEXT("Highlights"), LOCTEXT("ColorGrading_HighlightsLabel", "Highlights")));

	AddDetailsViewPropertyToGroup(GroupPropertyHandle, ColorGradingGroup, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingRenderingSettings, AutoExposureBias), TEXT("Exposure"));
	AddDetailsViewPropertyToGroup(GroupPropertyHandle, ColorGradingGroup, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingRenderingSettings, ColorCorrectionShadowsMax), TEXT("Color Grading"));
	AddDetailsViewPropertyToGroup(GroupPropertyHandle, ColorGradingGroup, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingRenderingSettings, ColorCorrectionHighlightsMin), TEXT("Color Grading"));
	AddDetailsViewPropertyToGroup(GroupPropertyHandle, ColorGradingGroup, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingRenderingSettings, ColorCorrectionHighlightsMax), TEXT("Color Grading"));
	AddDetailsViewPropertyToGroup(GroupPropertyHandle, ColorGradingGroup, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingWhiteBalanceSettings, TemperatureType), TEXT("White Balance"));
	AddDetailsViewPropertyToGroup(GroupPropertyHandle, ColorGradingGroup, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingWhiteBalanceSettings, WhiteTemp), TEXT("White Balance"));
	AddDetailsViewPropertyToGroup(GroupPropertyHandle, ColorGradingGroup, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingWhiteBalanceSettings, WhiteTint), TEXT("White Balance"));
	AddDetailsViewPropertyToGroup(GroupPropertyHandle, ColorGradingGroup, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingMiscSettings, BlueCorrection), TEXT("Misc"));
	AddDetailsViewPropertyToGroup(GroupPropertyHandle, ColorGradingGroup, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingMiscSettings, ExpandGamut), TEXT("Misc"));
	AddDetailsViewPropertyToGroup(GroupPropertyHandle, ColorGradingGroup, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingMiscSettings, SceneColorTint), TEXT("Misc"));

	// Ensure the details view categories are always displayed in a specific order
	ColorGradingGroup.DetailsViewCategorySortOrder =
	{
		{ CATEGORY_OVERRIDE_NAME("Exposure"), 0},
		{ CATEGORY_OVERRIDE_NAME("Color Grading"), 1},
		{ CATEGORY_OVERRIDE_NAME("White Balance"), 2},
		{ CATEGORY_OVERRIDE_NAME("Misc"), 3}
	};

	return ColorGradingGroup;
}

FDisplayClusterColorGradingDataModel::FColorGradingElement FDisplayClusterColorGradingGenerator_ColorGradingRenderingSettings::CreateColorGradingElement(
	const TSharedPtr<IPropertyHandle>& GroupPropertyHandle,
	FName ElementPropertyName,
	FText ElementLabel)
{
	FDisplayClusterColorGradingDataModel::FColorGradingElement ColorGradingElement;
	ColorGradingElement.DisplayName = ElementLabel;

	TSharedPtr<IPropertyHandle> ElementPropertyHandle = GroupPropertyHandle->GetChildHandle(ElementPropertyName);
	if (ElementPropertyHandle.IsValid() && ElementPropertyHandle->IsValidHandle())
	{
		ColorGradingElement.SaturationPropertyHandle = ElementPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingSettings, Saturation));
		ColorGradingElement.ContrastPropertyHandle = ElementPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingSettings, Contrast));
		ColorGradingElement.GammaPropertyHandle = ElementPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingSettings, Gamma));
		ColorGradingElement.GainPropertyHandle = ElementPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingSettings, Gain));
		ColorGradingElement.OffsetPropertyHandle = ElementPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_ColorGradingSettings, Offset));
	}

	return ColorGradingElement;
}

void FDisplayClusterColorGradingGenerator_ColorGradingRenderingSettings::AddDetailsViewPropertyToGroup(
	const TSharedPtr<IPropertyHandle>& GroupPropertyHandle,
	FDisplayClusterColorGradingDataModel::FColorGradingGroup& Group,
	FName PropertyName,
	const FString& CategoryOverride)
{
	TSharedPtr<IPropertyHandle> PropertyHandle = GroupPropertyHandle->GetChildHandle(PropertyName);
	if (PropertyHandle.IsValid() && PropertyHandle->IsValidHandle())
	{
		PropertyHandle->SetInstanceMetaData(TEXT("CategoryOverride"), CategoryOverride);

		Group.DetailsViewPropertyHandles.Add(PropertyHandle);
	}
}

TSharedPtr<IDetailTreeNode> FDisplayClusterColorGradingGenerator_ColorGradingRenderingSettings::FindPropertyTreeNode(const TSharedRef<IDetailTreeNode>& Node, const FCachedPropertyPath& PropertyPath)
{
	if (Node->GetNodeType() == EDetailNodeType::Item)
	{
		FName NodeName = Node->GetNodeName();
		if (Node->GetNodeName() == PropertyPath.GetLastSegment().GetName())
		{
			TSharedPtr<IPropertyHandle> FoundPropertyHandle = Node->CreatePropertyHandle();
			FString FoundPropertyPath = FoundPropertyHandle->GeneratePathToProperty();

			if (PropertyPath == FoundPropertyPath)
			{
				return Node;
			}
		}
		
		return nullptr;
	}
	else
	{
		TArray<TSharedRef<IDetailTreeNode>> Children;
		Node->GetChildren(Children);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			if (TSharedPtr<IDetailTreeNode> PropertyTreeNode = FindPropertyTreeNode(Child, PropertyPath))
			{
				return PropertyTreeNode;
			}
		}

		return nullptr;
	}
}

TSharedPtr<IPropertyHandle> FDisplayClusterColorGradingGenerator_ColorGradingRenderingSettings::FindPropertyHandle(IPropertyRowGenerator& PropertyRowGenerator, const FCachedPropertyPath& PropertyPath)
{
	const TArray<TSharedRef<IDetailTreeNode>>& RootNodes = PropertyRowGenerator.GetRootTreeNodes();

	for (const TSharedRef<IDetailTreeNode>& RootNode : RootNodes)
	{
		if (TSharedPtr<IDetailTreeNode> PropertyTreeNode = FindPropertyTreeNode(RootNode, PropertyPath))
		{
			return PropertyTreeNode->CreatePropertyHandle();
		}
	}

	return nullptr;
}

#define CREATE_PROPERTY_PATH(RootObjectClass, PropertyPath) FCachedPropertyPath(GET_MEMBER_NAME_STRING_CHECKED(RootObjectClass, PropertyPath))

TSharedRef<IDisplayClusterColorGradingDataModelGenerator> FDisplayClusterColorGradingGenerator_RootActor::MakeInstance()
{
	return MakeShareable(new FDisplayClusterColorGradingGenerator_RootActor());
}

class FRootActorDetailsSectionCustomization : public IDetailCustomization
{
public:
	enum EDetailsSectionType
	{
		Viewports,
		InnerFrustum,
		OCIO_AllViewports,
		OCIO_PerViewport
	};

public:
	FRootActorDetailsSectionCustomization(EDetailsSectionType InSectionType)
		: SectionType(InSectionType)
	{ }

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		TArray<FName> Categories;
		DetailBuilder.GetCategoryNames(Categories);

		for (const FName& Category : Categories)
		{
			DetailBuilder.HideCategory(Category);
		}

		// TransformCommon is a custom category that doesn't get returned by GetCategoryNames that also needs to be hidden
		DetailBuilder.HideCategory(TEXT("TransformCommon"));

		switch (SectionType)
		{
		case EDetailsSectionType::Viewports:
			{
				IDetailCategoryBuilder& ViewportsCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomViewportsCategory"), LOCTEXT("CustomViewportsCategoryLabel", "Viewports"));

				ViewportsCategoryBuilder.AddProperty(TEXT("ViewportScreenPercentageMultiplierRef"), ADisplayClusterRootActor::StaticClass());
				ViewportsCategoryBuilder.AddProperty(TEXT("FreezeRenderOuterViewportsRef"), ADisplayClusterRootActor::StaticClass());
			}
			break;

		case EDetailsSectionType::InnerFrustum:
			{
				IDetailCategoryBuilder& InnerFrustumCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomICVFXCategory"), LOCTEXT("CustomICVFXCategoryLabel", "In-Camera VFX"));

				InnerFrustumCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, InnerFrustumPriority), ADisplayClusterRootActor::StaticClass());
			}
			break;

		case EDetailsSectionType::OCIO_AllViewports:
			{
				IDetailCategoryBuilder& AllViewportsOCIOCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomAllViewportsOCIOCategory"), LOCTEXT("CustomAllViewportsOCIOCategoryLabel", "All Viewports"));

				AllViewportsOCIOCategoryBuilder.AddProperty(TEXT("ClusterOCIOColorConfigurationRef"), ADisplayClusterRootActor::StaticClass());
			}
			break;

		case EDetailsSectionType::OCIO_PerViewport:
			{
				IDetailCategoryBuilder& PerViewportOCIOCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomPerViewportOCIOCategory"), LOCTEXT("CustomPerViewportOCIOCategoryLabel", "Per-Viewport"));

				PerViewportOCIOCategoryBuilder.AddProperty(TEXT("PerViewportOCIOProfilesRef"), ADisplayClusterRootActor::StaticClass());
			}
			break;
		}
	}

private:
	EDetailsSectionType SectionType;
};

void FDisplayClusterColorGradingGenerator_RootActor::GenerateDataModel(IPropertyRowGenerator& PropertyRowGenerator, FDisplayClusterColorGradingDataModel& OutColorGradingDataModel)
{
	RootActors.Empty();

	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = PropertyRowGenerator.GetSelectedObjects();
	for (const TWeakObjectPtr<UObject>& SelectedObject : SelectedObjects)
	{
		if (SelectedObject.IsValid() && SelectedObject->IsA<ADisplayClusterRootActor>())
		{
			TWeakObjectPtr<ADisplayClusterRootActor> SelectedRootActor = CastChecked<ADisplayClusterRootActor>(SelectedObject.Get());
			RootActors.Add(SelectedRootActor);
		}
	}

	// Add a color grading group for the root actor's "EntireClusterColorGrading" property
	if (TSharedPtr<IPropertyHandle> EntireClusterColorGradingHandle = FindPropertyHandle(PropertyRowGenerator, CREATE_PROPERTY_PATH(UDisplayClusterConfigurationData, StageSettings.EntireClusterColorGrading)))
	{
		FDisplayClusterColorGradingDataModel::FColorGradingGroup EntireClusterGroup = CreateColorGradingGroup(EntireClusterColorGradingHandle);
		EntireClusterGroup.EditConditionPropertyHandle = EntireClusterColorGradingHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_EntireClusterColorGrading, bEnableEntireClusterColorGrading));

		EntireClusterGroup.GroupHeaderWidget = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 4, 0)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(EntireClusterGroup.DisplayName)
						.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						CreateViewportComboBox(INDEX_NONE)
					];

		OutColorGradingDataModel.ColorGradingGroups.Add(EntireClusterGroup);
	}

	// Add a color grading group for each element in the root actor's "PerViewportColorGrading" array
	if (TSharedPtr<IPropertyHandle> PerViewportColorGradingHandle = FindPropertyHandle(PropertyRowGenerator, CREATE_PROPERTY_PATH(UDisplayClusterConfigurationData, StageSettings.PerViewportColorGrading)))
	{
		check(PerViewportColorGradingHandle->AsArray().IsValid());

		uint32 NumGroups;
		if (PerViewportColorGradingHandle->AsArray()->GetNumElements(NumGroups) == FPropertyAccess::Success)
		{
			for (int32 Index = 0; Index < (int32)NumGroups; ++Index)
			{
				TSharedRef<IPropertyHandle> PerViewportElementHandle = PerViewportColorGradingHandle->AsArray()->GetElement(Index);

				FDisplayClusterColorGradingDataModel::FColorGradingGroup PerViewportGroup = CreateColorGradingGroup(PerViewportElementHandle);
				PerViewportGroup.EditConditionPropertyHandle = PerViewportElementHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_PerViewportColorGrading, bIsEnabled));

				// Add per-viewport group specific properties and force the category of these properties to be at the top
				AddDetailsViewPropertyToGroup(PerViewportElementHandle, 
					PerViewportGroup, 
					GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_PerViewportColorGrading, bIsEntireClusterEnabled),
					TEXT("Per-Viewport Settings"));

				PerViewportGroup.DetailsViewCategorySortOrder.Add(CATEGORY_OVERRIDE_NAME("Per-Viewport Settings"), -1);

				TSharedPtr<IPropertyHandle> NamePropertyHandle = PerViewportElementHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_PerViewportColorGrading, Name));
				if (NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle())
				{
					NamePropertyHandle->GetValue(PerViewportGroup.DisplayName);
				}

				PerViewportGroup.GroupHeaderWidget = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 4, 0)
					.VAlign(VAlign_Center)
					[
						SNew(SInlineEditableTextBlock)
						.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
						.OnTextCommitted_Lambda([NamePropertyHandle](const FText& InText, ETextCommit::Type TextCommitType)
						{
							if (NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle())
							{
								NamePropertyHandle->SetValue(InText);
								IDisplayClusterColorGrading::Get().GetColorGradingDrawerSingleton().RefreshColorGradingDrawers(true);
							}
						})
						.Text_Lambda([NamePropertyHandle]()
						{
							FText Name = FText::GetEmpty();

							if (NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle())
							{
								NamePropertyHandle->GetValue(Name);
							}

							return Name;
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						CreateViewportComboBox(Index)
					];

				OutColorGradingDataModel.ColorGradingGroups.Add(PerViewportGroup);
			}
		}
	}

	OutColorGradingDataModel.bShowColorGradingGroupToolBar = true;
	OutColorGradingDataModel.ColorGradingGroupToolBarWidget = SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.OnClicked(this, &FDisplayClusterColorGradingGenerator_RootActor::AddColorGradingGroup)
		.ContentPadding(FMargin(1.0f, 0.0f))
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];


	FDisplayClusterColorGradingDataModel::FDetailsSection ViewportsDetailsSection;
	ViewportsDetailsSection.DisplayName = LOCTEXT("ViewportsDetailsSectionLabel", "Viewports");
	ViewportsDetailsSection.DetailCustomizationDelegate = FOnGetDetailCustomizationInstance::CreateLambda([]
	{
		return MakeShared<FRootActorDetailsSectionCustomization>(FRootActorDetailsSectionCustomization::EDetailsSectionType::Viewports);
	});

	OutColorGradingDataModel.DetailsSections.Add(ViewportsDetailsSection);

	FDisplayClusterColorGradingDataModel::FDetailsSection InnerFrustumDetailsSection;
	InnerFrustumDetailsSection.DisplayName = LOCTEXT("InnerFrustumDetailsSectionLabel", "Inner Frustum");
	InnerFrustumDetailsSection.EditConditionPropertyHandle = FindPropertyHandle(PropertyRowGenerator, CREATE_PROPERTY_PATH(UDisplayClusterConfigurationData, StageSettings.bEnableInnerFrustums));
	InnerFrustumDetailsSection.DetailCustomizationDelegate = FOnGetDetailCustomizationInstance::CreateLambda([]
	{
		return MakeShared<FRootActorDetailsSectionCustomization>(FRootActorDetailsSectionCustomization::EDetailsSectionType::InnerFrustum);
	});

	OutColorGradingDataModel.DetailsSections.Add(InnerFrustumDetailsSection);

	FDisplayClusterColorGradingDataModel::FDetailsSection OCIODetailsSection;
	OCIODetailsSection.DisplayName = LOCTEXT("OCIODetailsSectionLabel", "OCIO");
	OCIODetailsSection.EditConditionPropertyHandle = FindPropertyHandle(PropertyRowGenerator, CREATE_PROPERTY_PATH(UDisplayClusterConfigurationData, StageSettings.bUseOverallClusterOCIOConfiguration));

	FDisplayClusterColorGradingDataModel::FDetailsSubsection AllViewportsOCIODetailsSubsection;
	AllViewportsOCIODetailsSubsection.DisplayName = LOCTEXT("AllViewportsOCIOSubsectionLabel", "All Viewports");
	AllViewportsOCIODetailsSubsection.DetailCustomizationDelegate = FOnGetDetailCustomizationInstance::CreateLambda([]
	{
		return MakeShared<FRootActorDetailsSectionCustomization>(FRootActorDetailsSectionCustomization::EDetailsSectionType::OCIO_AllViewports);
	});

	OCIODetailsSection.Subsections.Add(AllViewportsOCIODetailsSubsection);

	FDisplayClusterColorGradingDataModel::FDetailsSubsection PerViewportOCIODetailsSubsection;
	PerViewportOCIODetailsSubsection.DisplayName = LOCTEXT("PerViewportOCIOSubsectionLabel", "Per-Viewport");
	PerViewportOCIODetailsSubsection.DetailCustomizationDelegate = FOnGetDetailCustomizationInstance::CreateLambda([]
	{
		return MakeShared<FRootActorDetailsSectionCustomization>(FRootActorDetailsSectionCustomization::EDetailsSectionType::OCIO_PerViewport);
	});

	OCIODetailsSection.Subsections.Add(PerViewportOCIODetailsSubsection);

	OutColorGradingDataModel.DetailsSections.Add(OCIODetailsSection);
}

FReply FDisplayClusterColorGradingGenerator_RootActor::AddColorGradingGroup()
{
	for (const TWeakObjectPtr<ADisplayClusterRootActor>& RootActor : RootActors)
	{
		if (RootActor.IsValid())
		{
			if (UDisplayClusterConfigurationData* ConfigData = RootActor->GetConfigData())
			{
				FScopedTransaction Transaction(LOCTEXT("AddViewportColorGradingGroupTransaction", "Add Viewport Group"));

				RootActor->Modify();
				ConfigData->Modify();

				FDisplayClusterConfigurationViewport_PerViewportColorGrading NewColorGradingGroup;
				NewColorGradingGroup.Name = LOCTEXT("NewViewportColorGradingGroupName", "NewViewportGroup");

				ConfigData->StageSettings.PerViewportColorGrading.Add(NewColorGradingGroup);

				IDisplayClusterColorGrading::Get().GetColorGradingDrawerSingleton().RefreshColorGradingDrawers(true);
			}
		}
	}

	return FReply::Handled();
}

TSharedRef<SWidget> FDisplayClusterColorGradingGenerator_RootActor::CreateViewportComboBox(int32 PerViewportColorGradingIndex) const
{
	return SNew(SComboButton)
		.HasDownArrow(true)
		.OnGetMenuContent(this, &FDisplayClusterColorGradingGenerator_RootActor::GetViewportComboBoxMenu, PerViewportColorGradingIndex)
		.ButtonContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FDisplayClusterColorGradingStyle::Get().GetBrush("ColorGradingDrawer.Viewports"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &FDisplayClusterColorGradingGenerator_RootActor::GetViewportComboBoxText, PerViewportColorGradingIndex)
			]
		];
}

FText FDisplayClusterColorGradingGenerator_RootActor::GetViewportComboBoxText(int32 PerViewportColorGradingIndex) const
{
	// For now, only support displaying actual data when a single root actor is selected
	if (RootActors.Num() == 1)
	{
		TWeakObjectPtr<ADisplayClusterRootActor> RootActor = RootActors[0];

		if (RootActor.IsValid())
		{
			if (UDisplayClusterConfigurationData* ConfigData = RootActor->GetConfigData())
			{
				// If a valid per-viewport color grading group is passed in, determine the number of viewports associated with that group; otherwise,
				// count the total viewports in the configuration
				int32 NumViewports = 0;
				if (PerViewportColorGradingIndex > INDEX_NONE && PerViewportColorGradingIndex < ConfigData->StageSettings.PerViewportColorGrading.Num())
				{
					FDisplayClusterConfigurationViewport_PerViewportColorGrading& PerViewportColorGrading = ConfigData->StageSettings.PerViewportColorGrading[PerViewportColorGradingIndex];
					NumViewports = PerViewportColorGrading.ApplyPostProcessToObjects.Num();
				}
				else
				{
					for (const TTuple<FString, TObjectPtr<UDisplayClusterConfigurationClusterNode>>& Node : ConfigData->Cluster->Nodes)
					{
						if (Node.Value)
						{
							NumViewports += Node.Value->Viewports.Num();
						}
					}
				}

				return FText::Format(LOCTEXT("PerViewportColorGradingGroup_NumViewports", "{0} {0}|plural(one=Viewport,other=Viewports)"), FText::AsNumber(NumViewports));
			}
		}
	}
	else if (RootActors.Num() > 1)
	{
		return LOCTEXT("MultipleValuesSelectedLabel", "Multiple Values");
	}

	return FText::GetEmpty();
}

TSharedRef<SWidget> FDisplayClusterColorGradingGenerator_RootActor::GetViewportComboBoxMenu(int32 PerViewportColorGradingIndex) const
{
	FMenuBuilder MenuBuilder(false, nullptr);

	// For now, only support displaying actual data when a single root actor is selected
	if (RootActors.Num() == 1)
	{
		TWeakObjectPtr<ADisplayClusterRootActor> RootActor = RootActors[0];
		if (RootActor.IsValid())
		{
			if (UDisplayClusterConfigurationData* ConfigData = RootActor->GetConfigData())
			{
				// Extract all viewport names from the configuration data, so they can be sorted alphabetically
				TArray<FString> ViewportNames;

				for (const TTuple<FString, TObjectPtr<UDisplayClusterConfigurationClusterNode>>& Node : ConfigData->Cluster->Nodes)
				{
					if (Node.Value)
					{
						for (const TTuple<FString, TObjectPtr<UDisplayClusterConfigurationViewport>>& Viewport : Node.Value->Viewports)
						{
							ViewportNames.Add(Viewport.Key);
						}
					}
				}

				ViewportNames.Sort();

				const bool bForEntireCluster = PerViewportColorGradingIndex == INDEX_NONE;
				FDisplayClusterConfigurationViewport_PerViewportColorGrading* PerViewportColorGrading = !bForEntireCluster ? 
					&ConfigData->StageSettings.PerViewportColorGrading[PerViewportColorGradingIndex] : nullptr;

				MenuBuilder.BeginSection(TEXT("ViewportSection"), LOCTEXT("ViewportsMenuSectionLabel", "Viewports"));
				for (const FString& ViewportName : ViewportNames)
				{
					MenuBuilder.AddMenuEntry(FText::FromString(ViewportName),
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([ConfigData , PerViewportColorGrading, ViewportName, InRootActor=RootActor.Get()]
							{
								if (PerViewportColorGrading)
								{
									if (PerViewportColorGrading->ApplyPostProcessToObjects.Contains(ViewportName))
									{
										FScopedTransaction Transaction(LOCTEXT("RemoveViewportFromColorGradingGroupTransaction", "Remove Viewport from Group"));
										InRootActor->Modify();
										ConfigData->Modify();

										PerViewportColorGrading->ApplyPostProcessToObjects.Remove(ViewportName);
									}
									else
									{
										FScopedTransaction Transaction(LOCTEXT("AddViewportToColorGradingGroupTransaction", "Add Viewport to Group"));
										InRootActor->Modify();
										ConfigData->Modify();

										PerViewportColorGrading->ApplyPostProcessToObjects.Add(ViewportName);
									}
								}
							}),
							FCanExecuteAction::CreateLambda([PerViewportColorGrading] { return PerViewportColorGrading != nullptr; }),
							FGetActionCheckState::CreateLambda([PerViewportColorGrading, ViewportName]
							{
								// If the menu is for the EntireCluster group (PerViewportColorGrading is null), all viewport list items should be checked
								if (PerViewportColorGrading)
								{
									const bool bIsViewportInGroup = PerViewportColorGrading->ApplyPostProcessToObjects.Contains(ViewportName);
									return bIsViewportInGroup ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								}
								else
								{
									return ECheckBoxState::Checked;
								}
							})
						),
						NAME_None,
						EUserInterfaceActionType::ToggleButton);
				}
				MenuBuilder.EndSection();
			}
		}
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<IDisplayClusterColorGradingDataModelGenerator> FDisplayClusterColorGradingGenerator_ICVFXCamera::MakeInstance()
{
	return MakeShareable(new FDisplayClusterColorGradingGenerator_ICVFXCamera());
}

class FICVFXCameraDetailsSectionCustomization : public IDetailCustomization
{
public:
	enum EDetailsSectionType
	{
		ICVFX,
		Overscan,
		Chromakey_Markers,
		Chromakey_Custom,
		OCIO_AllNodes,
		OCIO_PerNode
	};

public:
	FICVFXCameraDetailsSectionCustomization(EDetailsSectionType InSectionType)
		: SectionType(InSectionType)
	{ }

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		TArray<FName> Categories;
		DetailBuilder.GetCategoryNames(Categories);

		for (const FName& Category : Categories)
		{
			DetailBuilder.HideCategory(Category);
		}

		// TransformCommon is a custom category that doesn't get returned by GetCategoryNames that also needs to be hidden
		DetailBuilder.HideCategory(TEXT("TransformCommon"));

		switch (SectionType)
		{
		case EDetailsSectionType::ICVFX:
			{
				IDetailCategoryBuilder& ICVFXCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomICVFXCategory"), LOCTEXT("CustomICVFXCategoryLabel", "In-Camera VFX"));
				AddProperty(DetailBuilder, ICVFXCategoryBuilder, TEXT("BufferRatioRef"));
				AddProperty(DetailBuilder, ICVFXCategoryBuilder, TEXT("ExternalCameraActorRef"));
				AddProperty(DetailBuilder, ICVFXCategoryBuilder, TEXT("HiddenICVFXViewportsRef"));

				IDetailCategoryBuilder& SoftEdgeCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomSoftEdgeCategory"), LOCTEXT("CustomSoftEdgeCategoryLabel", "Soft Edge"));
				AddProperty(DetailBuilder, SoftEdgeCategoryBuilder, TEXT("SoftEdgeRef"), true);

				IDetailCategoryBuilder& BorderCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomBorderCategory"), LOCTEXT("CustomBorderCategoryLabel", "Border"));
				AddProperty(DetailBuilder, BorderCategoryBuilder, TEXT("BorderRef"), true);
			}
			break;

		case EDetailsSectionType::Overscan:
			{
				IDetailCategoryBuilder& OverscanCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomOverscanCategory"), LOCTEXT("CustomOverscanCategoryLabel", "Inner Frustum Overscan"));
				AddProperty(DetailBuilder, OverscanCategoryBuilder, TEXT("CustomFrustumRef"), true);
			}
			break;

		case EDetailsSectionType::Chromakey_Markers:
			{
				IDetailCategoryBuilder& ChromakeyCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomChromakeyCategory"), LOCTEXT("CustomChromakeyCategoryLabel", "Chromakey"));
				AddProperty(DetailBuilder, ChromakeyCategoryBuilder, TEXT("ChromakeyColorRef"));

				IDetailCategoryBuilder& ChromakeyMarkersCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomChromakeyMarkersCategory"), LOCTEXT("CustomChromakeyMarkersCategoryLabel", "ChromakeyMarkers"));
				AddProperty(DetailBuilder, ChromakeyMarkersCategoryBuilder, TEXT("ChromakeyMarkersRef"), true);
			}
			break;

		case EDetailsSectionType::Chromakey_Custom:
			{
				IDetailCategoryBuilder& ChromakeyCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomChromakeyCategory"), LOCTEXT("CustomChromakeyCategoryLabel", "Chromakey"));
				AddProperty(DetailBuilder, ChromakeyCategoryBuilder, TEXT("ChromakeyColorRef"));

				IDetailCategoryBuilder& ChromakeyCustomCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomChromakeyCustomCategory"), LOCTEXT("CustomChromakeyCustomCategoryLabel", "Custom Chromakey"));
				AddProperty(DetailBuilder, ChromakeyCustomCategoryBuilder, TEXT("ChromakeyRenderTextureRef"), true);
			}
			break;

		case EDetailsSectionType::OCIO_AllNodes:
			{
				IDetailCategoryBuilder& OCIOAllNodesCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomAllNodesOCIOCategory"), LOCTEXT("CustomAllNodesOCIOCategoryLabel", "All Nodes"));
				AddProperty(DetailBuilder, OCIOAllNodesCategoryBuilder, TEXT("OCIOColorConfiguratonRef"));
			}
			break;

		case EDetailsSectionType::OCIO_PerNode:
			{
				IDetailCategoryBuilder& OCIOPerNodeCategoryBuilder = DetailBuilder.EditCategory(TEXT("CustomPerNodeOCIOCategory"), LOCTEXT("CustomPerNodeOCIOCategoryLabel", "Per-Node"));
				AddProperty(DetailBuilder, OCIOPerNodeCategoryBuilder, TEXT("PerNodeOCIOProfilesRef"));
			}
			break;
		}
	}

private:
	void AddProperty(IDetailLayoutBuilder& DetailBuilder, IDetailCategoryBuilder& Category, FName PropertyName, bool bExpandChildProperties = false)
	{
		TSharedRef<IPropertyHandle> PropertyHandle = DetailBuilder.GetProperty(PropertyName, UDisplayClusterICVFXCameraComponent::StaticClass());

		if (bExpandChildProperties)
		{
			PropertyHandle->SetInstanceMetaData(TEXT("ShowOnlyInnerProperties"), TEXT("1"));
		}

		Category.AddProperty(PropertyHandle);
	}

private:
	EDetailsSectionType SectionType;
};

void FDisplayClusterColorGradingGenerator_ICVFXCamera::GenerateDataModel(IPropertyRowGenerator& PropertyRowGenerator, FDisplayClusterColorGradingDataModel& OutColorGradingDataModel)
{
	CameraComponents.Empty();

	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = PropertyRowGenerator.GetSelectedObjects();
	for (const TWeakObjectPtr<UObject>& SelectedObject : SelectedObjects)
	{
		if (SelectedObject.IsValid() && SelectedObject->IsA<UDisplayClusterICVFXCameraComponent>())
		{
			TWeakObjectPtr<UDisplayClusterICVFXCameraComponent> SelectedCameraComponent = CastChecked<UDisplayClusterICVFXCameraComponent>(SelectedObject.Get());
			CameraComponents.Add(SelectedCameraComponent);
		}
	}

	// Add a color grading group for the camera's "AllNodesColorGrading" property
	if (TSharedPtr<IPropertyHandle> AllNodesColorGradingHandle = FindPropertyHandle(PropertyRowGenerator, CREATE_PROPERTY_PATH(UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesColorGrading)))
	{
		FDisplayClusterColorGradingDataModel::FColorGradingGroup AllNodesGroup = CreateColorGradingGroup(AllNodesColorGradingHandle);
		AllNodesGroup.EditConditionPropertyHandle = AllNodesColorGradingHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_AllNodesColorGrading, bEnableInnerFrustumAllNodesColorGrading));

		AllNodesGroup.GroupHeaderWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(AllNodesGroup.DisplayName)
				.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				CreateNodeComboBox(INDEX_NONE)
			];

		OutColorGradingDataModel.ColorGradingGroups.Add(AllNodesGroup);
	}

	// Add a color grading group for each element in the camera's "PerNodeColorGrading" array
	if (TSharedPtr<IPropertyHandle> PerNodeColorGradingHandle = FindPropertyHandle(PropertyRowGenerator, CREATE_PROPERTY_PATH(UDisplayClusterICVFXCameraComponent, CameraSettings.PerNodeColorGrading)))
	{
		check(PerNodeColorGradingHandle->AsArray().IsValid());

		uint32 NumGroups;
		if (PerNodeColorGradingHandle->AsArray()->GetNumElements(NumGroups) == FPropertyAccess::Success)
		{
			for (int32 Index = 0; Index < (int32)NumGroups; ++Index)
			{
				TSharedRef<IPropertyHandle> PerNodeElementHandle = PerNodeColorGradingHandle->AsArray()->GetElement(Index);

				FDisplayClusterColorGradingDataModel::FColorGradingGroup PerNodeGroup = CreateColorGradingGroup(PerNodeElementHandle);
				PerNodeGroup.EditConditionPropertyHandle = PerNodeElementHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_PerNodeColorGrading, bIsEnabled));

				// Add per-node group specific properties and force the category of these properties to be at the top
				AddDetailsViewPropertyToGroup(PerNodeElementHandle,
					PerNodeGroup,
					GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_PerNodeColorGrading, bEntireClusterColorGrading),
					TEXT("Per-Node Settings"));

				AddDetailsViewPropertyToGroup(PerNodeElementHandle,
					PerNodeGroup,
					GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_PerNodeColorGrading, bAllNodesColorGrading),
					TEXT("Per-Node Settings"));

				PerNodeGroup.DetailsViewCategorySortOrder.Add(CATEGORY_OVERRIDE_NAME("Per-Node Settings"), -1);

				TSharedPtr<IPropertyHandle> NamePropertyHandle = PerNodeElementHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationViewport_PerNodeColorGrading, Name));
				if (NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle())
				{
					NamePropertyHandle->GetValue(PerNodeGroup.DisplayName);
				}

				PerNodeGroup.GroupHeaderWidget = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 4, 0)
					.VAlign(VAlign_Center)
					[
						SNew(SInlineEditableTextBlock)
						.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
						.OnTextCommitted_Lambda([NamePropertyHandle](const FText& InText, ETextCommit::Type TextCommitType)
						{
							if (NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle())
							{
								NamePropertyHandle->SetValue(InText);
								IDisplayClusterColorGrading::Get().GetColorGradingDrawerSingleton().RefreshColorGradingDrawers(true);
							}
						})
						.Text_Lambda([NamePropertyHandle]()
						{
							FText Name = FText::GetEmpty();

							if (NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle())
							{
								NamePropertyHandle->GetValue(Name);
							}

							return Name;
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						CreateNodeComboBox(Index)
					];

				OutColorGradingDataModel.ColorGradingGroups.Add(PerNodeGroup);
			}
		}
	}

	OutColorGradingDataModel.bShowColorGradingGroupToolBar = true;OutColorGradingDataModel.bShowColorGradingGroupToolBar = true;
	OutColorGradingDataModel.ColorGradingGroupToolBarWidget = SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.OnClicked(this, &FDisplayClusterColorGradingGenerator_ICVFXCamera::AddColorGradingGroup)
		.ContentPadding(FMargin(1.0f, 0.0f))
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];

	{
		FDisplayClusterColorGradingDataModel::FDetailsSection InnerFrustumDetailsSection;
		InnerFrustumDetailsSection.DisplayName = LOCTEXT("InnerFrustumDetailsSectionLabel", "Inner Frustum");
		InnerFrustumDetailsSection.EditConditionPropertyHandle = FindPropertyHandle(PropertyRowGenerator, CREATE_PROPERTY_PATH(UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable));

		FDisplayClusterColorGradingDataModel::FDetailsSubsection ICVFXDetailsSubsection;
		ICVFXDetailsSubsection.DisplayName = LOCTEXT("ICVFXSubsectionLabel", "ICVFX");
		ICVFXDetailsSubsection.DetailCustomizationDelegate = FOnGetDetailCustomizationInstance::CreateLambda([]
		{
			return MakeShared<FICVFXCameraDetailsSectionCustomization>(FICVFXCameraDetailsSectionCustomization::EDetailsSectionType::ICVFX);
		});

		InnerFrustumDetailsSection.Subsections.Add(ICVFXDetailsSubsection);

		FDisplayClusterColorGradingDataModel::FDetailsSubsection OverscanDetailsSubsection;
		OverscanDetailsSubsection.DisplayName = LOCTEXT("OverscanDetailsSubsectionLabel", "Overscan");
		OverscanDetailsSubsection.DetailCustomizationDelegate = FOnGetDetailCustomizationInstance::CreateLambda([]
		{
			return MakeShared<FICVFXCameraDetailsSectionCustomization>(FICVFXCameraDetailsSectionCustomization::EDetailsSectionType::Overscan);
		});

		InnerFrustumDetailsSection.Subsections.Add(OverscanDetailsSubsection);

		OutColorGradingDataModel.DetailsSections.Add(InnerFrustumDetailsSection);
	}

	{
		FDisplayClusterColorGradingDataModel::FDetailsSection ChromakeyDetailsSection;
		ChromakeyDetailsSection.DisplayName = LOCTEXT("ChromakeyDetailsSectionLabel", "Chromakey");
		ChromakeyDetailsSection.EditConditionPropertyHandle = FindPropertyHandle(PropertyRowGenerator, CREATE_PROPERTY_PATH(UDisplayClusterICVFXCameraComponent, CameraSettings.Chromakey.bEnable));

		FDisplayClusterColorGradingDataModel::FDetailsSubsection ChromakeyMarkersDetailsSubsection;
		ChromakeyMarkersDetailsSubsection.DisplayName = LOCTEXT("ChromakeyMarkersDetailsSubsectionLabel", "Markers");
		ChromakeyMarkersDetailsSubsection.DetailCustomizationDelegate = FOnGetDetailCustomizationInstance::CreateLambda([]
		{
			return MakeShared<FICVFXCameraDetailsSectionCustomization>(FICVFXCameraDetailsSectionCustomization::EDetailsSectionType::Chromakey_Markers);
		});

		ChromakeyDetailsSection.Subsections.Add(ChromakeyMarkersDetailsSubsection);

		FDisplayClusterColorGradingDataModel::FDetailsSubsection ChromakeyCustomDetailsSubsection;
		ChromakeyCustomDetailsSubsection.DisplayName = LOCTEXT("ChromakeyCustomDetailsSubsectionLabel", "Custom");
		ChromakeyCustomDetailsSubsection.DetailCustomizationDelegate = FOnGetDetailCustomizationInstance::CreateLambda([]
		{
			return MakeShared<FICVFXCameraDetailsSectionCustomization>(FICVFXCameraDetailsSectionCustomization::EDetailsSectionType::Chromakey_Custom);
		});

		ChromakeyDetailsSection.Subsections.Add(ChromakeyCustomDetailsSubsection);

		OutColorGradingDataModel.DetailsSections.Add(ChromakeyDetailsSection);
	}

	{
		FDisplayClusterColorGradingDataModel::FDetailsSection OCIODetailsSection;
		OCIODetailsSection.DisplayName = LOCTEXT("OCIODetailsSectionLabel", "OCIO");
		OCIODetailsSection.EditConditionPropertyHandle = FindPropertyHandle(PropertyRowGenerator, CREATE_PROPERTY_PATH(UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesOCIOConfiguration.bIsEnabled));

		FDisplayClusterColorGradingDataModel::FDetailsSubsection AllNodesOCIODetailsSubsection;
		AllNodesOCIODetailsSubsection.DisplayName = LOCTEXT("AllNodesOCIODetailsSubsectionLabel", "All Nodes");
		AllNodesOCIODetailsSubsection.DetailCustomizationDelegate = FOnGetDetailCustomizationInstance::CreateLambda([]
		{
			return MakeShared<FICVFXCameraDetailsSectionCustomization>(FICVFXCameraDetailsSectionCustomization::EDetailsSectionType::OCIO_AllNodes);
		});

		OCIODetailsSection.Subsections.Add(AllNodesOCIODetailsSubsection);

		FDisplayClusterColorGradingDataModel::FDetailsSubsection PerNodeOCIODetailsSubsection;
		PerNodeOCIODetailsSubsection.DisplayName = LOCTEXT("PerNodeOCIODetailsSubsectionLabel", "Per-Node");
		PerNodeOCIODetailsSubsection.DetailCustomizationDelegate = FOnGetDetailCustomizationInstance::CreateLambda([]
		{
			return MakeShared<FICVFXCameraDetailsSectionCustomization>(FICVFXCameraDetailsSectionCustomization::EDetailsSectionType::OCIO_PerNode);
		});

		OCIODetailsSection.Subsections.Add(PerNodeOCIODetailsSubsection);

		OutColorGradingDataModel.DetailsSections.Add(OCIODetailsSection);
	}
}

FReply FDisplayClusterColorGradingGenerator_ICVFXCamera::AddColorGradingGroup()
{
	for (const TWeakObjectPtr<UDisplayClusterICVFXCameraComponent>& CameraComponent : CameraComponents)
	{
		if (CameraComponent.IsValid())
		{
			FScopedTransaction Transaction(LOCTEXT("AddNodeColorGradingGroupTransaction", "Add Node Group"));
			CameraComponent->Modify();

			FDisplayClusterConfigurationViewport_PerNodeColorGrading NewColorGradingGroup;
			NewColorGradingGroup.Name = LOCTEXT("NewNodeColorGradingGroupName", "NewNodeGroup");

			CameraComponent->CameraSettings.PerNodeColorGrading.Add(NewColorGradingGroup);

			IDisplayClusterColorGrading::Get().GetColorGradingDrawerSingleton().RefreshColorGradingDrawers(true);
		}
	}

	return FReply::Handled();
}

TSharedRef<SWidget> FDisplayClusterColorGradingGenerator_ICVFXCamera::CreateNodeComboBox(int32 PerNodeColorGradingIndex) const
{
	return SNew(SComboButton)
		.HasDownArrow(true)
		.OnGetMenuContent(this, &FDisplayClusterColorGradingGenerator_ICVFXCamera::GetNodeComboBoxMenu, PerNodeColorGradingIndex)
		.ButtonContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FDisplayClusterColorGradingStyle::Get().GetBrush("ColorGradingDrawer.Nodes"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &FDisplayClusterColorGradingGenerator_ICVFXCamera::GetNodeComboBoxText, PerNodeColorGradingIndex)
			]
		];
}

FText FDisplayClusterColorGradingGenerator_ICVFXCamera::GetNodeComboBoxText(int32 PerNodeColorGradingIndex) const
{
	// For now, only support displaying actual data when a single camera component is selected
	if (CameraComponents.Num() == 1)
	{
		TWeakObjectPtr<UDisplayClusterICVFXCameraComponent> CameraComponent = CameraComponents[0];
		if (CameraComponent.IsValid())
		{
			int32 NumNodes = 0;
			if (PerNodeColorGradingIndex > INDEX_NONE && PerNodeColorGradingIndex < CameraComponent->CameraSettings.PerNodeColorGrading.Num())
			{
				const FDisplayClusterConfigurationViewport_PerNodeColorGrading& PerNodeColorGrading = CameraComponent->CameraSettings.PerNodeColorGrading[PerNodeColorGradingIndex];
				NumNodes = PerNodeColorGrading.ApplyPostProcessToObjects.Num();
			}
			else
			{
				if (ADisplayClusterRootActor* ParentRootActor = Cast<ADisplayClusterRootActor>(CameraComponent->GetOwner()))
				{
					if (UDisplayClusterConfigurationData* ConfigData = ParentRootActor->GetConfigData())
					{
						NumNodes = ConfigData->Cluster->Nodes.Num();
					}
				}
			}

			return FText::Format(LOCTEXT("PerNodeColorGradingGroup_NumViewports", "{0} {0}|plural(one=Node,other=Nodes)"), FText::AsNumber(NumNodes));
		}
	}
	else if (CameraComponents.Num() > 1)
	{
		return LOCTEXT("MultipleValuesSelectedLabel", "Multiple Values");
	}

	return FText::GetEmpty();
}


TSharedRef<SWidget> FDisplayClusterColorGradingGenerator_ICVFXCamera::GetNodeComboBoxMenu(int32 PerNodeColorGradingIndex) const
{

	FMenuBuilder MenuBuilder(false, nullptr);

	// For now, only support displaying actual data when a single camera component is selected
	if (CameraComponents.Num() == 1)
	{
		TWeakObjectPtr<UDisplayClusterICVFXCameraComponent> CameraComponent = CameraComponents[0];
		if (CameraComponent.IsValid())
		{
			if (ADisplayClusterRootActor* ParentRootActor = Cast<ADisplayClusterRootActor>(CameraComponent->GetOwner()))
			{
				if (UDisplayClusterConfigurationData* ConfigData = ParentRootActor->GetConfigData())
				{
					TArray<FString> NodeNames;
					ConfigData->Cluster->Nodes.GetKeys(NodeNames);

					NodeNames.Sort();

					const bool bForAllNodes = PerNodeColorGradingIndex == INDEX_NONE;
					FDisplayClusterConfigurationViewport_PerNodeColorGrading* PerNodeColorGrading = !bForAllNodes ?
						&CameraComponent->CameraSettings.PerNodeColorGrading[PerNodeColorGradingIndex] : nullptr;

					MenuBuilder.BeginSection(TEXT("NodeSection"), LOCTEXT("NodeMenuSectionLabel", "Nodes"));
					for (const FString& NodeName : NodeNames)
					{
						MenuBuilder.AddMenuEntry(FText::FromString(NodeName),
							FText::GetEmpty(),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([PerNodeColorGrading, NodeName, InCameraComponent=CameraComponent.Get()]()
								{
									if (PerNodeColorGrading)
									{
										if (PerNodeColorGrading->ApplyPostProcessToObjects.Contains(NodeName))
										{
											FScopedTransaction Transaction(LOCTEXT("RemoveNodeFromColorGradingGroupTransaction", "Remove Node from Group"));
											InCameraComponent->Modify();

											PerNodeColorGrading->ApplyPostProcessToObjects.Remove(NodeName);
										}
										else
										{
											FScopedTransaction Transaction(LOCTEXT("AddNodeToColorGradingGroupTransaction", "Add Node to Group"));
											InCameraComponent->Modify();

											PerNodeColorGrading->ApplyPostProcessToObjects.Add(NodeName);
										}
									}
								}),
								FCanExecuteAction::CreateLambda([PerNodeColorGrading] { return PerNodeColorGrading != nullptr; }),
								FGetActionCheckState::CreateLambda([PerNodeColorGrading, NodeName]()
								{
									// If the menu is for the AllNodes group (PerNodeColorGrading is null), all node list items should be checked
									if (PerNodeColorGrading)
									{
										const bool bIsNodeInGroup = PerNodeColorGrading->ApplyPostProcessToObjects.Contains(NodeName);
										return bIsNodeInGroup ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
									}
									else
									{
										return ECheckBoxState::Checked;
									}
								})
							),
							NAME_None,
							EUserInterfaceActionType::ToggleButton);
					}
					MenuBuilder.EndSection();
				}
			}
		}
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE