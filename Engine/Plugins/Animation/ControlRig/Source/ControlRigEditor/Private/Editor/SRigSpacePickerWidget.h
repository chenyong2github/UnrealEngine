// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRig.h"
#include "Rigs/RigHierarchy.h"
#include "SRigHierarchy.h"
#include "Widgets/Layout/SBox.h"
#include "Rigs/RigSpaceHierarchy.h"

DECLARE_EVENT_ThreeParams(SRigSpacePickerWidget, FRigSpacePickerActiveSpaceChanged, URigHierarchy*, const FRigElementKey&, const FRigElementKey&);
DECLARE_EVENT_ThreeParams(SRigSpacePickerWidget, FRigSpacePickerSpaceListChanged, URigHierarchy*, const FRigElementKey&, const TArray<FRigElementKey>&);
DECLARE_DELEGATE_RetVal_TwoParams(TArray<FRigElementKey>, FRigSpacePickerGetAdditionalSpaces, URigHierarchy*, const FRigElementKey&);

/** Widget allowing picking of a space source for space switching */
class SRigSpacePickerWidget : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SRigSpacePickerWidget)
		: _Hierarchy(nullptr)
		, _Control()
		, _Customization(nullptr)
		, _ShowDefaultSpaces(true)
		, _ShowFavoriteSpaces(true)
		, _ShowAdditionalSpaces(true)
		, _AllowReorder(false)
		, _AllowDelete(false)
		, _AllowAdd(false)
		, _ShowBakeButton(false)
		, _Title()
		, _BackgroundBrush(FEditorStyle::GetBrush("Menu.Background"))
		{}
		SLATE_ARGUMENT(URigHierarchy*, Hierarchy)
		SLATE_ARGUMENT(FRigElementKey, Control)
		SLATE_ARGUMENT(FRigControlElementCustomization*, Customization)
		SLATE_ARGUMENT(bool, ShowDefaultSpaces)
		SLATE_ARGUMENT(bool, ShowFavoriteSpaces)
		SLATE_ARGUMENT(bool, ShowAdditionalSpaces)
		SLATE_ARGUMENT(bool, AllowReorder)
		SLATE_ARGUMENT(bool, AllowDelete)
		SLATE_ARGUMENT(bool, AllowAdd)
		SLATE_ARGUMENT(bool, ShowBakeButton)
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(const FSlateBrush*, BackgroundBrush)
		SLATE_ARGUMENT(FRigSpacePickerGetAdditionalSpaces, GetAdditionalSpacesDelegate)
		SLATE_EVENT( FOnClicked, OnBakeButtonClicked )
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SRigSpacePickerWidget() override;

	void SetControl(URigHierarchy* InHierarchy, const FRigElementKey& InControl, FRigControlElementCustomization* InCustomization = nullptr);

	FReply OpenDialog(bool bModal = true);
	void CloseDialog();
	
	virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) override;
	virtual bool SupportsKeyboardFocus() const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	const FRigElementKey& GetActiveSpace() const;
	TArray<FRigElementKey> GetDefaultSpaces() const;
	TArray<FRigElementKey> GetSpaceList(bool bIncludeDefaultSpaces = false) const;
	FRigSpacePickerActiveSpaceChanged& OnActiveSpaceChanged() { return ActiveSpaceChangedEvent; }
	FRigSpacePickerSpaceListChanged& OnSpaceListChanged() { return SpaceListChangedEvent; }

private:

	enum ESpacePickerType
	{
		ESpacePickerType_Parent,
		ESpacePickerType_World,
		ESpacePickerType_Item
	};

	void AddSpacePickerRow(
		TSharedPtr<SVerticalBox> InListBox,
		ESpacePickerType InType,
		const FRigElementKey& InKey,
		const FSlateBrush* InBush,
		const FText& InTitle,
		FOnClicked OnClickedDelegate);

	void RepopulateItemSpaces();
	void ClearListBox(TSharedPtr<SVerticalBox> InListBox);
	void UpdateActiveSpace();
	bool IsValidKey(const FRigElementKey& InKey) const;
	bool IsDefaultSpace(const FRigElementKey& InKey) const;

	FReply HandleParentSpaceClicked();
	FReply HandleWorldSpaceClicked();
	FReply HandleElementSpaceClicked(FRigElementKey InKey);
	FReply HandleSpaceMoveUp(FRigElementKey InKey);
	FReply HandleSpaceMoveDown(FRigElementKey InKey);
	void HandleSpaceDelete(FRigElementKey InKey);
	FReply HandleAddElementClicked();
	bool IsSpaceMoveUpEnabled(FRigElementKey InKey) const;
	bool IsSpaceMoveDownEnabled(FRigElementKey InKey) const;
	const URigHierarchy* GetHierarchy() const { return Hierarchy; }

	void OnHierarchyModified(ERigHierarchyNotification InNotif, URigHierarchy* InHierarchy, const FRigBaseElement* InElement);

	FSlateColor GetButtonColor(ESpacePickerType InType, FRigElementKey InKey) const;
	TArray<FRigElementKey> GetCurrentParents(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey) const;
	
	FRigSpacePickerActiveSpaceChanged ActiveSpaceChangedEvent;
	FRigSpacePickerSpaceListChanged SpaceListChangedEvent;

	URigHierarchy* Hierarchy;
	FRigElementKey ControlKey;
	FRigElementKey DefaultParentKey;
	FRigElementKey WorldSocketKey;
	TArray<FRigElementKey> CurrentSpaceKeys;
	bool bRepopulateRequired;

	FRigControlElementCustomization* Customization;
	bool bShowDefaultSpaces;
	bool bShowFavoriteSpaces;
	bool bShowAdditionalSpaces;
	bool bAllowReorder;
	bool bAllowDelete;
	bool bAllowAdd;
	bool bShowBakeButton;
	bool bLaunchingContextMenu;

	FRigSpacePickerGetAdditionalSpaces GetAdditionalSpacesDelegate; 
	TArray<FRigElementKey> AdditionalSpaces;

	TSharedPtr<SVerticalBox> TopLevelListBox;
	TSharedPtr<SVerticalBox> ItemSpacesListBox;
	TSharedPtr<SHorizontalBox> BottomButtonsListBox;
	TWeakPtr<SWindow> DialogWindow;
	TWeakPtr<IMenu> ContextMenu;
	FRigElementKey ActiveSpace;
	FDelegateHandle HierarchyModifiedHandle;
	FDelegateHandle ActiveSpaceChangedWindowHandle;

	static FRigElementKey InValidKey;
};
