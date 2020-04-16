// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraPlatformSetCustomization.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEmitter.h"
#include "NiagaraPlatformSet.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "PlatformInfo.h"
#include "PropertyHandle.h"
#include "Scalability.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Layout/Visibility.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "NiagaraSettings.h"

#define LOCTEXT_NAMESPACE "FNiagaraPlatformSetCustomization"

void FNiagaraPlatformSetCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	TargetPlatformSet = (FNiagaraPlatformSet*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);

	if (PlatformSelectionStates.Num() == 0)
	{
		PlatformSelectionStates.Add(MakeShared<ENiagaraPlatformSelectionState>(ENiagaraPlatformSelectionState::Enabled));
		PlatformSelectionStates.Add(MakeShared<ENiagaraPlatformSelectionState>(ENiagaraPlatformSelectionState::Disabled));
		PlatformSelectionStates.Add(MakeShared<ENiagaraPlatformSelectionState>(ENiagaraPlatformSelectionState::Default));
	}

	//Look for outer types for which we need to ensure there are no conflicting settings.
	SystemScalabilitySettings = nullptr;
	EmitterScalabilitySettings = nullptr;
	SystemScalabilityOverrides = nullptr;
	EmitterScalabilityOverrides = nullptr;
	PlatformSetArray.Reset();
	PlatformSetArrayIndex = INDEX_NONE;

	//Look whether this platform set belongs to a class which must keep orthogonal platform sets.
	//We then interrogate the other sets via these ptrs to look for conflicts.
	TSharedPtr<IPropertyHandle> CurrHandle = PropertyHandle->GetParentHandle();
	while (CurrHandle)
	{
		int32 ThisIndex = CurrHandle->GetIndexInArray();
		if (ThisIndex != INDEX_NONE)
		{
			PlatformSetArray = CurrHandle->GetParentHandle()->AsArray();
			PlatformSetArrayIndex = ThisIndex;
		}

		if (FProperty* CurrProperty = CurrHandle->GetProperty())
		{
			if (UStruct* CurrStruct = CurrProperty->GetOwnerStruct())
			{
				TSharedPtr<IPropertyHandle> ParentHandle = CurrHandle->GetParentHandle();
				if (CurrStruct == FNiagaraSystemScalabilitySettingsArray::StaticStruct())
				{
					SystemScalabilitySettings = (FNiagaraSystemScalabilitySettingsArray*)ParentHandle->GetValueBaseAddress((uint8*)Objects[0]);
					break;
				}
				else if (CurrStruct == FNiagaraEmitterScalabilitySettingsArray::StaticStruct())
				{
					EmitterScalabilitySettings = (FNiagaraEmitterScalabilitySettingsArray*)ParentHandle->GetValueBaseAddress((uint8*)Objects[0]);
					break;
				}
				else if (CurrStruct == FNiagaraSystemScalabilityOverrides::StaticStruct())
				{
					SystemScalabilityOverrides = (FNiagaraSystemScalabilityOverrides*)ParentHandle->GetValueBaseAddress((uint8*)Objects[0]);
					break;
				}
				else if (CurrStruct == FNiagaraEmitterScalabilityOverrides::StaticStruct())
				{
					EmitterScalabilityOverrides = (FNiagaraEmitterScalabilityOverrides*)ParentHandle->GetValueBaseAddress((uint8*)Objects[0]);
					break;
				}
			}
		}

		CurrHandle = CurrHandle->GetParentHandle();
	}

	UpdateCachedConflicts();
	
	BaseSystem = Objects[0]->GetTypedOuter<UNiagaraSystem>();
	PropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FNiagaraPlatformSetCustomization::OnPropertyValueChanged));

	HeaderRow
		.WholeRowContent()
		[ 	
			SAssignNew(QualityLevelWidgetBox, SWrapBox)
			.UseAllottedSize(true)
		];

	GenerateQualityLevelSelectionWidgets();
}

void FNiagaraPlatformSetCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

FText FNiagaraPlatformSetCustomization::GetCurrentText() const
{
	return LOCTEXT("Platforms", "Platforms");
}

FText FNiagaraPlatformSetCustomization::GetTooltipText() const
{
	return LOCTEXT("Platforms", "Platforms");
}

void FNiagaraPlatformSetCustomization::GenerateQualityLevelSelectionWidgets()
{
	QualityLevelWidgetBox->ClearChildren();

	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	check(Settings);

	int32 NumQualityLevels = Settings->QualityLevels.Num();
	
	QualityLevelMenuAnchors.SetNum(NumQualityLevels);
	QualityLevelMenuContents.SetNum(NumQualityLevels);

	for (int32 QualityLevel = 0; QualityLevel < NumQualityLevels; ++QualityLevel)
	{
		bool First = QualityLevel == 0;
		bool Last = QualityLevel == (NumQualityLevels - 1);

		if (!QualityLevelMenuAnchors[QualityLevel].IsValid())
		{
			QualityLevelMenuAnchors[QualityLevel] = SNew(SMenuAnchor)
				.ToolTipText(LOCTEXT("AddPlatformOverride", "Add an override for a specific platform."))
				.OnGetMenuContent(this, &FNiagaraPlatformSetCustomization::GenerateDeviceProfileTreeWidget, QualityLevel)
				[
					SNew(SButton)
					.Visibility_Lambda([this, QualityLevel]()
					{
						return QualityLevelWidgetBox->GetChildren()->GetChildAt(QualityLevel)->IsHovered() ? EVisibility::Visible : EVisibility::Hidden;
					})
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ForegroundColor(FSlateColor::UseForeground())
					.OnClicked(this, &FNiagaraPlatformSetCustomization::ToggleMenuOpenForQualityLevel, QualityLevel)
					[
						SNew(SBox)
						.WidthOverride(8)
						.HeightOverride(8)
						.VAlign(VAlign_Center)
						[
							SNew(SImage)
							.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.DropdownButton"))
						]
					]
				];
		}

		QualityLevelWidgetBox->AddSlot()
			.Padding(0, 0, 1, 0)
			[
				SNew(SBox)
				.WidthOverride(100)
				.VAlign(VAlign_Top)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SCheckBox)
						.Style(FNiagaraEditorStyle::Get(), First ? "NiagaraEditor.PlatformSet.StartButton" : 
							(Last ? "NiagaraEditor.PlatformSet.EndButton" : "NiagaraEditor.PlatformSet.MiddleButton"))
						.IsChecked(this, &FNiagaraPlatformSetCustomization::IsQLChecked, QualityLevel)
						.OnCheckStateChanged(this, &FNiagaraPlatformSetCustomization::QLCheckStateChanged, QualityLevel)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.HAlign(HAlign_Fill)
							.Padding(6,2,2,4)
							[
								SNew(STextBlock)
								.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.PlatformSet.ButtonText")
								.Text(FNiagaraPlatformSet::GetQualityLevelText(QualityLevel))
								.ColorAndOpacity(this, &FNiagaraPlatformSetCustomization::GetQualityLevelButtonTextColor, QualityLevel)
								.ShadowOffset(FVector2D(1, 1))
							]
							// error icon
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							.Padding(0,0,2,0)
							[
								SNew(SBox)
								.WidthOverride(12)
								.HeightOverride(12)
								[
									SNew(SImage)
									.ToolTipText(this, &FNiagaraPlatformSetCustomization::GetQualityLevelErrorToolTip, QualityLevel)
									.Visibility(this, &FNiagaraPlatformSetCustomization::GetQualityLevelErrorVisibility, QualityLevel)
									.Image(FEditorStyle::GetBrush("Icons.Error"))
								]
							]
							// dropdown button
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							.Padding(0,0,2,0)
							[
								QualityLevelMenuAnchors[QualityLevel].ToSharedRef()
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						GenerateAdditionalDevicesWidgetForQL(QualityLevel)
					]
				]
			];
	}
}

void FNiagaraPlatformSetCustomization::UpdateCachedConflicts()
{
	TArray<const FNiagaraPlatformSet*> PlatformSets;
	if (SystemScalabilityOverrides != nullptr)
	{
		for (FNiagaraSystemScalabilityOverride& Override : SystemScalabilityOverrides->Overrides)
		{
			PlatformSets.Add(&Override.Platforms);
		}
	}
	else if (EmitterScalabilityOverrides != nullptr)
	{
		for (FNiagaraEmitterScalabilityOverride& Override : EmitterScalabilityOverrides->Overrides)
		{
			PlatformSets.Add(&Override.Platforms);
		}
	}
	else if (SystemScalabilitySettings != nullptr)
	{
		for (FNiagaraSystemScalabilitySettings& Settings : SystemScalabilitySettings->Settings)
		{
			PlatformSets.Add(&Settings.Platforms);
		}
	}
	else if (EmitterScalabilitySettings != nullptr)
	{
		for (FNiagaraEmitterScalabilitySettings& Settings : EmitterScalabilitySettings->Settings)
		{
			PlatformSets.Add(&Settings.Platforms);
		}
	}

	CachedConflicts.Reset();
	FNiagaraPlatformSet::GatherConflicts(PlatformSets, CachedConflicts);
}

static TSharedPtr<IPropertyHandle> FindChildPlatformSet(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	if (FStructProperty* Property = CastField<FStructProperty>(PropertyHandle->GetProperty()))
	{
		if (Property->Struct == FNiagaraPlatformSet::StaticStruct())
		{
			return PropertyHandle;
		}
	}

	// recurse
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (int32 Idx = 0; Idx < (int32) NumChildren; ++Idx)
	{
		TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(Idx);

		TSharedPtr<IPropertyHandle> ChildResult = FindChildPlatformSet(Child);
		if (ChildResult.IsValid())
		{
			return ChildResult;
		}
	}

	return TSharedPtr<IPropertyHandle>();
}

void FNiagaraPlatformSetCustomization::InvalidateSiblingConflicts() const
{
	if (!PlatformSetArray.IsValid())
	{
		return;
	}

	uint32 ArrayCount = 0;
	PlatformSetArray->GetNumElements(ArrayCount);
	for (int32 Idx = 0; Idx < (int32) ArrayCount; ++Idx)
	{
		if (Idx == PlatformSetArrayIndex)
		{
			continue; // skip self
		}

		TSharedRef<IPropertyHandle> Sibling = PlatformSetArray->GetElement(Idx);

		TSharedPtr<IPropertyHandle> SiblingPlatformSet = FindChildPlatformSet(Sibling);
		if (SiblingPlatformSet.IsValid())
		{
			SiblingPlatformSet->NotifyPostChange();
		}
	}
}

EVisibility FNiagaraPlatformSetCustomization::GetQualityLevelErrorVisibility(int32 QualityLevel) const
{
	// not part of an array
	if (PlatformSetArrayIndex == INDEX_NONE)
	{
		return EVisibility::Collapsed;
	}

	for (const FNiagaraPlatformSetConflictInfo& ConflictInfo : CachedConflicts)
	{
		if (ConflictInfo.SetAIndex == PlatformSetArrayIndex || 
			ConflictInfo.SetBIndex == PlatformSetArrayIndex)
		{
			// this conflict applies to this platform set, check if it applies to this quality leve button
			const int32 QLMask = FNiagaraPlatformSet::CreateQualityLevelMask(QualityLevel);

			for (const FNiagaraPlatformSetConflictEntry& Conflict : ConflictInfo.Conflicts)
			{
				if ((QLMask & Conflict.QualityLevelMask) != 0)
				{
					return EVisibility::Visible;
				}
			}
		}
	}

	return EVisibility::Collapsed;
}

FText FNiagaraPlatformSetCustomization::GetQualityLevelErrorToolTip(int32 QualityLevel) const
{
	if (PlatformSetArrayIndex == INDEX_NONE)
	{
		return FText::GetEmpty();
	}

	for (const FNiagaraPlatformSetConflictInfo& ConflictInfo : CachedConflicts)
	{
		int32 OtherIndex = INDEX_NONE;

		if (ConflictInfo.SetAIndex == PlatformSetArrayIndex)
		{
			OtherIndex = ConflictInfo.SetBIndex;
		}

		if (ConflictInfo.SetBIndex == PlatformSetArrayIndex)
		{
			OtherIndex = ConflictInfo.SetAIndex;
		}

		if (OtherIndex != INDEX_NONE)
		{
			const int32 QLMask = FNiagaraPlatformSet::CreateQualityLevelMask(QualityLevel);

			for (const FNiagaraPlatformSetConflictEntry& Conflict : ConflictInfo.Conflicts)
			{
				if ((QLMask & Conflict.QualityLevelMask) != 0)
				{
					FText FormatString = LOCTEXT("QualityLevelConflictToolTip", "This effect quality conflicts with the set at index {Index} in this array."); 

					FFormatNamedArguments Args;
					Args.Add(TEXT("Index"), OtherIndex);

					return FText::Format(FormatString, Args);
				}
			}
		}
	}

	return FText::GetEmpty();
}

FSlateColor FNiagaraPlatformSetCustomization::GetQualityLevelButtonTextColor(int32 QualityLevel) const
{
	return TargetPlatformSet->IsEffectQualityEnabled(QualityLevel) ? 
		FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f)) :
		FSlateColor::UseForeground();
}

TSharedRef<SWidget> FNiagaraPlatformSetCustomization::GenerateAdditionalDevicesWidgetForQL(int32 QualityLevel)
{
	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	auto AddDeviceProfileOverrideWidget = [&](UDeviceProfile* Profile, bool bEnabled)
	{
		TSharedPtr<SHorizontalBox> DeviceBox;

		Container->AddSlot()
			.AutoHeight()
			[
				SAssignNew(DeviceBox, SHorizontalBox)
			];

		const FText DeviceNameText = FText::FromName(Profile->GetFName());

		DeviceBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(3, 0, 3, 0)
			[
				SNew(SBox)
				.WidthOverride(8)
				.HeightOverride(8)
				.ToolTipText(bEnabled ?
					LOCTEXT("DeviceIncludedToolTip", "This device is included.") :
					LOCTEXT("DeviceExcludedToolTip", "This device is excluded."))
				[
					SNew(SImage)
					.Image(bEnabled ?
						FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Include") :
						FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Exclude"))
					.ColorAndOpacity(bEnabled ?
						FLinearColor(0, 1, 0) :
						FLinearColor(1, 0, 0))
					]
				];

		DeviceBox->AddSlot()
			.HAlign(HAlign_Fill)
			.Padding(0, 0, 2, 0)
			[
				SNew(STextBlock)
				.Text(DeviceNameText)
				.ToolTipText(DeviceNameText)
			];

		DeviceBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(SBox)
				.WidthOverride(12)
				.HeightOverride(12)
				[
					SNew(SImage)
					.ToolTipText(this, &FNiagaraPlatformSetCustomization::GetDeviceProfileErrorToolTip, Profile, QualityLevel)
					.Visibility(this, &FNiagaraPlatformSetCustomization::GetDeviceProfileErrorVisibility, Profile, QualityLevel)
					.Image(FEditorStyle::GetBrush("Icons.Error"))
				]
			];

		DeviceBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ForegroundColor(FSlateColor::UseForeground())
				.OnClicked(this, &FNiagaraPlatformSetCustomization::RemoveDeviceProfile, Profile, QualityLevel)
				.ToolTipText(LOCTEXT("RemoveDevice", "Remove this device override."))
				[
					SNew(SBox)
					.WidthOverride(8)
					.HeightOverride(8)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Remove"))
					]
				]
			];
		};

	TArray<UDeviceProfile*> EnabledProfiles;
	TArray<UDeviceProfile*> DisabledProfiles;
	TargetPlatformSet->GetOverridenDeviceProfiles(QualityLevel, EnabledProfiles, DisabledProfiles);

	for (UDeviceProfile* Profile : EnabledProfiles)
	{
		AddDeviceProfileOverrideWidget(Profile, true);
	}

	for (UDeviceProfile* Profile : DisabledProfiles)
	{
		AddDeviceProfileOverrideWidget(Profile, false);
	}

	return Container;
}

FReply FNiagaraPlatformSetCustomization::RemoveDeviceProfile(UDeviceProfile* Profile, int32 QualityLevel)
{
	PropertyHandle->NotifyPreChange();
	TargetPlatformSet->SetDeviceProfileState(Profile, QualityLevel, ENiagaraPlatformSelectionState::Default);
	PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	InvalidateSiblingConflicts();

	return FReply::Handled();
}

EVisibility FNiagaraPlatformSetCustomization::GetDeviceProfileErrorVisibility(UDeviceProfile* Profile, int32 QualityLevel) const
{
	// not part of an array
	if (PlatformSetArrayIndex == INDEX_NONE)
	{
		return EVisibility::Collapsed;
	}

	for (const FNiagaraPlatformSetConflictInfo& ConflictInfo : CachedConflicts)
	{
		if (ConflictInfo.SetAIndex == PlatformSetArrayIndex || 
			ConflictInfo.SetBIndex == PlatformSetArrayIndex)
		{
			const int32 QLMask = FNiagaraPlatformSet::CreateQualityLevelMask(QualityLevel);

			for (const FNiagaraPlatformSetConflictEntry& Entry : ConflictInfo.Conflicts)
			{
				if ((Entry.QualityLevelMask & QLMask) != 0 &&
					Entry.ProfileName == Profile->GetFName())
				{
					return EVisibility::Visible;
				}
			}
		}
	}

	return EVisibility::Collapsed;
}

FText FNiagaraPlatformSetCustomization::GetDeviceProfileErrorToolTip(UDeviceProfile* Profile, int32 QualityLevel) const
{
	for (const FNiagaraPlatformSetConflictInfo& ConflictInfo : CachedConflicts)
	{
		int32 OtherIndex = INDEX_NONE;

		if (ConflictInfo.SetAIndex == PlatformSetArrayIndex)
		{
			OtherIndex = ConflictInfo.SetBIndex;
		}

		if (ConflictInfo.SetBIndex == PlatformSetArrayIndex)
		{
			OtherIndex = ConflictInfo.SetAIndex;
		}

		if (OtherIndex != INDEX_NONE)
		{
			const int32 QLMask = FNiagaraPlatformSet::CreateQualityLevelMask(QualityLevel);

			for (const FNiagaraPlatformSetConflictEntry& Entry : ConflictInfo.Conflicts)
			{
				if ((Entry.QualityLevelMask & QLMask) != 0 &&
					Entry.ProfileName == Profile->GetFName())
				{
					FText FormatString = LOCTEXT("PlatformOverrideConflictToolTip", "This platform override conflicts with the set at index {Index} in this array."); 

					FFormatNamedArguments Args;
					Args.Add(TEXT("Index"), OtherIndex);

					return FText::Format(FormatString, Args);
				}
			}
		}
	}

	return FText::GetEmpty();
}

FReply FNiagaraPlatformSetCustomization::ToggleMenuOpenForQualityLevel(int32 QualityLevel)
{
	check(QualityLevelMenuAnchors.IsValidIndex(QualityLevel));
	
	TSharedPtr<SMenuAnchor> MenuAnchor = QualityLevelMenuAnchors[QualityLevel];
	MenuAnchor->SetIsOpen(!MenuAnchor->IsOpen());

	return FReply::Handled();
}

// Is or does a viewmodel contain any children active at the given quality?
bool FNiagaraPlatformSetCustomization::IsTreeActiveForQL(const TSharedPtr<FNiagaraDeviceProfileViewModel>& Tree, int32 QualityLevelMask) const
{
	int32 Mask = TargetPlatformSet->GetEffectQualityMaskForDeviceProfile(Tree->Profile);
	if ((Mask & QualityLevelMask) != 0)
	{
		return true;
	}

	for (const TSharedPtr<FNiagaraDeviceProfileViewModel>& Child : Tree->Children)
	{
		if (IsTreeActiveForQL(Child, QualityLevelMask))
		{
			return true;
		}
	}

	return false;
}

void FNiagaraPlatformSetCustomization::FilterTreeForQL(const TSharedPtr<FNiagaraDeviceProfileViewModel>& SourceTree, TSharedPtr<FNiagaraDeviceProfileViewModel>& FilteredTree, int32 QualityLevelMask)
{
	for (const TSharedPtr<FNiagaraDeviceProfileViewModel>& SourceChild : SourceTree->Children)
	{
		if (IsTreeActiveForQL(SourceChild, QualityLevelMask))
		{
			TSharedPtr<FNiagaraDeviceProfileViewModel>& FilteredChild = FilteredTree->Children.Add_GetRef(MakeShared<FNiagaraDeviceProfileViewModel>());
			FilteredChild->Profile = SourceChild->Profile;

			FilterTreeForQL(SourceChild, FilteredChild, QualityLevelMask);
		}
	}
}

void FNiagaraPlatformSetCustomization::CreateDeviceProfileTree()
{
	//Pull device profiles out by their hierarchy depth.
	TArray<TArray<UDeviceProfile*>> DeviceProfilesByHierarchyLevel;
	for (UObject* Profile : UDeviceProfileManager::Get().Profiles)
	{
		UDeviceProfile* DeviceProfile = CastChecked<UDeviceProfile>(Profile);

		TFunction<void(int32&, UDeviceProfile*)> FindDepth;
		FindDepth = [&](int32& Depth, UDeviceProfile* CurrProfile)
		{
			if (CurrProfile->Parent)
			{
				FindDepth(++Depth, Cast<UDeviceProfile>(CurrProfile->Parent));
			}
		};

		int32 ProfileDepth = 0;
		FindDepth(ProfileDepth, DeviceProfile);
		DeviceProfilesByHierarchyLevel.SetNum(FMath::Max(ProfileDepth+1, DeviceProfilesByHierarchyLevel.Num()));
		DeviceProfilesByHierarchyLevel[ProfileDepth].Add(DeviceProfile);
	}
	
	FullDeviceProfileTree.Reset(DeviceProfilesByHierarchyLevel[0].Num());
	for (int32 RootProfileIdx = 0; RootProfileIdx < DeviceProfilesByHierarchyLevel[0].Num(); ++RootProfileIdx)
	{
		TFunction<void(FNiagaraDeviceProfileViewModel*, int32)> BuildProfileTree;
		BuildProfileTree = [&](FNiagaraDeviceProfileViewModel* CurrRoot, int32 CurrLevel)
		{
			int32 NextLevel = CurrLevel + 1;
			if (NextLevel < DeviceProfilesByHierarchyLevel.Num())
			{
				for (UDeviceProfile* PossibleChild : DeviceProfilesByHierarchyLevel[NextLevel])
				{
					if (PossibleChild->Parent == CurrRoot->Profile)
					{
						TSharedPtr<FNiagaraDeviceProfileViewModel>& NewChild = CurrRoot->Children.Add_GetRef(MakeShared<FNiagaraDeviceProfileViewModel>());
						NewChild->Profile = PossibleChild;
						BuildProfileTree(NewChild.Get(), NextLevel);
					}
				}
			}
		};

		//Add all root nodes and build their trees.
		TSharedPtr<FNiagaraDeviceProfileViewModel> CurrRoot = MakeShared<FNiagaraDeviceProfileViewModel>();
		CurrRoot->Profile = DeviceProfilesByHierarchyLevel[0][RootProfileIdx];
		BuildProfileTree(CurrRoot.Get(), 0);
		FullDeviceProfileTree.Add(CurrRoot);
	}

	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	check(Settings);

	int32 NumQualityLevels = Settings->QualityLevels.Num();
	FilteredDeviceProfileTrees.SetNum(NumQualityLevels);
	
	for (TSharedPtr<FNiagaraDeviceProfileViewModel>& FullDeviceRoot : FullDeviceProfileTree)
	{
		for (int32 QualityLevel = 0; QualityLevel < NumQualityLevels; ++QualityLevel)
		{
			int32 QualityLevelMask = FNiagaraPlatformSet::CreateQualityLevelMask(QualityLevel);

			if (IsTreeActiveForQL(FullDeviceRoot, QualityLevelMask))
			{
				TArray<TSharedPtr<FNiagaraDeviceProfileViewModel>>& FilteredRoots = FilteredDeviceProfileTrees[QualityLevel];

				TSharedPtr<FNiagaraDeviceProfileViewModel>& FilteredRoot = FilteredRoots.Add_GetRef(MakeShared<FNiagaraDeviceProfileViewModel>());
				FilteredRoot->Profile = FullDeviceRoot->Profile;

				FilterTreeForQL(FullDeviceRoot, FilteredRoot, QualityLevelMask);
			}
		}
	}
}

TSharedRef<SWidget> FNiagaraPlatformSetCustomization::GenerateDeviceProfileTreeWidget(int32 QualityLevel)
{
	if (FullDeviceProfileTree.Num() == 0)
	{
		CreateDeviceProfileTree();	
	}

	if (QualityLevelMenuContents[QualityLevel].IsValid())
	{
		return QualityLevelMenuContents[QualityLevel].ToSharedRef();
	}

	TArray<TSharedPtr<FNiagaraDeviceProfileViewModel>>* TreeToUse = &FullDeviceProfileTree;
	if (QualityLevel != INDEX_NONE)
	{
		check(QualityLevel < FilteredDeviceProfileTrees.Num());
		TreeToUse = &FilteredDeviceProfileTrees[QualityLevel];
	}

	return SAssignNew(QualityLevelMenuContents[QualityLevel], SBorder)
		.BorderImage(FEditorStyle::Get().GetBrush("Menu.Background"))
		[
			SAssignNew(DeviceProfileTreeWidget, STreeView<TSharedPtr<FNiagaraDeviceProfileViewModel>>)
			.TreeItemsSource(TreeToUse)
			.OnGenerateRow(this, &FNiagaraPlatformSetCustomization::OnGenerateDeviceProfileTreeRow, QualityLevel)
			.OnGetChildren(this, &FNiagaraPlatformSetCustomization::OnGetDeviceProfileTreeChildren, QualityLevel)
			.SelectionMode(ESelectionMode::None)
		];
}

TSharedRef<ITableRow> FNiagaraPlatformSetCustomization::OnGenerateDeviceProfileTreeRow(TSharedPtr<FNiagaraDeviceProfileViewModel> InItem, const TSharedRef<STableViewBase>& OwnerTable, int32 QualityLevel)
{
	TSharedPtr<SHorizontalBox> RowContainer;
	SAssignNew(RowContainer, SHorizontalBox);

	int32 ProfileMask = TargetPlatformSet->GetEffectQualityMaskForDeviceProfile(InItem->Profile);
	FText NameTooltip = FText::Format(LOCTEXT("ProfileQLTooltipFmt", "Effects Quality: {0}"), FNiagaraPlatformSet::GetQualityLevelMaskText(ProfileMask));
	
	//Top level profile. Look for a platform icon.
	if (InItem->Profile->Parent == nullptr)
	{
		if (const PlatformInfo::FPlatformInfo* Info = PlatformInfo::FindPlatformInfo(*InItem->Profile->DeviceType))
		{
			const FSlateBrush* DeviceProfileTypeIcon = FEditorStyle::GetBrush(Info->GetIconStyleName(PlatformInfo::EPlatformIconSize::Normal));
			if (DeviceProfileTypeIcon != FEditorStyle::Get().GetDefaultBrush())
			{
				RowContainer->AddSlot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					.Padding(4, 0, 0, 0)
					[
						SNew(SBox)
						.WidthOverride(16)
						.HeightOverride(16)
						[
							SNew(SImage)
							.Image(DeviceProfileTypeIcon)
						]
					];
			}
		}
	}

	FName TextStyleName("NormalText");
	FSlateColor TextColor(FSlateColor::UseForeground());
	ENiagaraPlatformSelectionState CurrentState = TargetPlatformSet->GetDeviceProfileState(InItem->Profile, QualityLevel);
	
	if (CurrentState == ENiagaraPlatformSelectionState::Enabled)
	{
		TextStyleName = "RichTextBlock.Bold";
		TextColor = FSlateColor(FLinearColor(FVector4(0,1,0,1)));
	}
	else if (CurrentState == ENiagaraPlatformSelectionState::Disabled)
	{
		TextStyleName = "RichTextBlock.Italic";
		TextColor = FSlateColor(FLinearColor(FVector4(1,0,0,1)));
	}


	int32 QualityLevelMask = FNiagaraPlatformSet::CreateQualityLevelMask(QualityLevel);
	if ((ProfileMask & QualityLevelMask) == 0)
	{
		TextColor = FSlateColor::UseSubduedForeground();
	}

	RowContainer->AddSlot()
		.Padding(4, 2, 0, 2)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.OnClicked(this, &FNiagaraPlatformSetCustomization::OnProfileMenuButtonClicked, InItem, QualityLevel, false)
			.IsEnabled(this, &FNiagaraPlatformSetCustomization::GetProfileMenuItemEnabled, InItem, QualityLevel)
			.ForegroundColor(TextColor)
			.ToolTipText(NameTooltip)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), TextStyleName)
				.Text(FText::FromString(InItem->Profile->GetName()))
			]
		];

	RowContainer->AddSlot()
		.AutoWidth()
		.Padding(12, 2, 4, 4)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Fill)
			.Visibility(this, &FNiagaraPlatformSetCustomization::GetProfileMenuButtonVisibility, InItem, QualityLevel)
			.OnClicked(this, &FNiagaraPlatformSetCustomization::OnProfileMenuButtonClicked, InItem, QualityLevel, true)
			.ToolTipText(this, &FNiagaraPlatformSetCustomization::GetProfileMenuButtonToolTip, InItem, QualityLevel)
			[
				SNew(SBox)
				.WidthOverride(8)
				.HeightOverride(8)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(this, &FNiagaraPlatformSetCustomization::GetProfileMenuButtonImage, InItem, QualityLevel)
				]
			]
		];

	return SNew(STableRow<TSharedPtr<FNiagaraDeviceProfileViewModel>>, OwnerTable)
		.Style(FNiagaraEditorStyle::Get(), "NiagaraEditor.PlatformSet.TreeView")
		[
			RowContainer.ToSharedRef()
		];
}

enum class EProfileButtonMode
{
	None,
	Include,
	Exclude,
	Remove
};

static EProfileButtonMode GetProfileMenuButtonMode(FNiagaraPlatformSet* PlatformSet, TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 QualityLevel)
{
	int32 Mask = PlatformSet->GetEffectQualityMaskForDeviceProfile(Item->Profile);
	int32 QLMask = FNiagaraPlatformSet::CreateQualityLevelMask(QualityLevel);

	if ((Mask & QLMask) == 0)
	{
		return EProfileButtonMode::None;
	}

	ENiagaraPlatformSelectionState CurrentState = PlatformSet->GetDeviceProfileState(Item->Profile, QualityLevel);

	bool bQualityEnabled = PlatformSet->IsEffectQualityEnabled(QualityLevel);
	bool bIsDefault = CurrentState == ENiagaraPlatformSelectionState::Default;

	if (bIsDefault && bQualityEnabled)
	{
		return EProfileButtonMode::Exclude;
	}

	if (bIsDefault && !bQualityEnabled)
	{
		return EProfileButtonMode::Include;
	}

	return EProfileButtonMode::Remove;
}


FText FNiagaraPlatformSetCustomization::GetProfileMenuButtonToolTip(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 QualityLevel) const
{
	EProfileButtonMode Mode = GetProfileMenuButtonMode(TargetPlatformSet, Item, QualityLevel);
	switch(Mode)
	{
		case EProfileButtonMode::Include:
			return LOCTEXT("IncludePlatform", "Include this platform.");
		case EProfileButtonMode::Exclude:
			return LOCTEXT("ExcludePlatform", "Exclude this platform.");
		case EProfileButtonMode::Remove:
			return LOCTEXT("RemovePlatform", "Remove this platform override.");
	}

	return FText::GetEmpty();
}

EVisibility FNiagaraPlatformSetCustomization::GetProfileMenuButtonVisibility(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 QualityLevel) const
{
	EProfileButtonMode Mode = GetProfileMenuButtonMode(TargetPlatformSet, Item, QualityLevel);
	if (Mode == EProfileButtonMode::None)
	{
		return EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}

bool FNiagaraPlatformSetCustomization::GetProfileMenuItemEnabled(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 QualityLevel) const
{
	EProfileButtonMode Mode = GetProfileMenuButtonMode(TargetPlatformSet, Item, QualityLevel);
	if (Mode == EProfileButtonMode::None)
	{
		return false;
	}

	return true;
}

const FSlateBrush* FNiagaraPlatformSetCustomization::GetProfileMenuButtonImage(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 QualityLevel) const
{
	EProfileButtonMode Mode = GetProfileMenuButtonMode(TargetPlatformSet, Item, QualityLevel);
	switch(Mode)
	{
		case EProfileButtonMode::Include:
			return FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Include");
		case EProfileButtonMode::Exclude:
			return FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Exclude");
		case EProfileButtonMode::Remove:
			return FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Remove");
	}

	return FEditorStyle::GetBrush("NoBrush");
}

FReply FNiagaraPlatformSetCustomization::OnProfileMenuButtonClicked(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 QualityLevel, bool bReopenMenu)
{
	ENiagaraPlatformSelectionState TargetState;

	EProfileButtonMode Mode = GetProfileMenuButtonMode(TargetPlatformSet, Item, QualityLevel);
	switch(Mode)
	{
		case EProfileButtonMode::Include:
			TargetState = ENiagaraPlatformSelectionState::Enabled;
			break;
		case EProfileButtonMode::Exclude:
			TargetState = ENiagaraPlatformSelectionState::Disabled;
			break;
		case EProfileButtonMode::Remove:
			TargetState = ENiagaraPlatformSelectionState::Default;
			break;
		default:
			return FReply::Handled(); // shouldn't happen, button should be collapsed
	}

	PropertyHandle->NotifyPreChange();
	TargetPlatformSet->SetDeviceProfileState(Item->Profile, QualityLevel, TargetState);
	PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	InvalidateSiblingConflicts();

	if (!bReopenMenu)
	{
		QualityLevelMenuAnchors[QualityLevel]->SetIsOpen(false);
	}

	DeviceProfileTreeWidget->RequestTreeRefresh();

	return FReply::Handled();
}

void FNiagaraPlatformSetCustomization::OnGetDeviceProfileTreeChildren(TSharedPtr<FNiagaraDeviceProfileViewModel> InItem, TArray< TSharedPtr<FNiagaraDeviceProfileViewModel> >& OutChildren, int32 QualityLevel)
{
	if (TargetPlatformSet->GetDeviceProfileState(InItem->Profile, QualityLevel) == ENiagaraPlatformSelectionState::Default)
	{
		OutChildren = InItem->Children;
	}
}

ECheckBoxState FNiagaraPlatformSetCustomization::IsQLChecked(int32 QualityLevel)const
{
	if (TargetPlatformSet)
	{
		return TargetPlatformSet->IsEffectQualityEnabled(QualityLevel) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Undetermined;
}

void FNiagaraPlatformSetCustomization::QLCheckStateChanged(ECheckBoxState CheckState, int32 QualityLevel)
{
	PropertyHandle->NotifyPreChange();
	TargetPlatformSet->SetEnabledForEffectQuality(QualityLevel, CheckState == ECheckBoxState::Checked);
	PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	InvalidateSiblingConflicts();
}

void FNiagaraPlatformSetCustomization::OnPropertyValueChanged()
{
	GenerateQualityLevelSelectionWidgets();
	UpdateCachedConflicts();
}

#undef LOCTEXT_NAMESPACE