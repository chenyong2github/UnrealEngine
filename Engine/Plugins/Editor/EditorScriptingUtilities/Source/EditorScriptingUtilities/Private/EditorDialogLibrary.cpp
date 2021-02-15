// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorDialogLibrary.h"
#include "Misc/MessageDialog.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "EditorStyleSet.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Editor.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "EditorDialogLibrary"

TEnumAsByte<EAppReturnType::Type> UEditorDialogLibrary::ShowMessage(const FText& Title, const FText& Message, TEnumAsByte<EAppMsgType::Type> MessageType, TEnumAsByte<EAppReturnType::Type> DefaultValue)
{
	return FMessageDialog::Open(MessageType, DefaultValue, Message, &Title);
}

/** Dialog widget used to display an object its properties */
class SObjParamDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SObjParamDialog) {}
	SLATE_END_ARGS()	

	void Construct(const FArguments& InArgs, TWeakPtr<SWindow> InParentWindow, const TArray<UObject*>& Objects)
	{
		bOKPressed = false;

		// Initialize details view
		FDetailsViewArgs DetailsViewArgs;
		{
			DetailsViewArgs.bLockable = false;
			DetailsViewArgs.bUpdatesFromSelection = false;
			DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ENameAreaSettings::ObjectsUseNameArea;
			DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
			DetailsViewArgs.bShowPropertyMatrixButton = false;
		}

		FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");		
		TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

		DetailsView->SetObjects(Objects, true);

		ChildSlot
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+SScrollBox::Slot()
				[
					DetailsView->AsShared()
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.Padding(2.0f)
					.AutoWidth()
					[
						SNew(SButton)
						.ContentPadding(FCoreStyle::Get().GetMargin("StandardDialog.ContentPadding"))
						.Text(LOCTEXT("OKButton", "OK"))
						.HAlign(HAlign_Center)
						.OnClicked_Lambda([this, InParentWindow, InArgs]()
						{
							if(InParentWindow.IsValid())
							{
								InParentWindow.Pin()->RequestDestroyWindow();
							}
							bOKPressed = true;
							return FReply::Handled(); 
						})
					]
					+SHorizontalBox::Slot()
					.Padding(2.0f)
					.AutoWidth()
					[
						SNew(SButton)
						.ContentPadding(FCoreStyle::Get().GetMargin("StandardDialog.ContentPadding"))
						.Text(LOCTEXT("CancelButton", "Cancel"))
						.HAlign(HAlign_Center)
						.OnClicked_Lambda([InParentWindow]()
						{ 
							if(InParentWindow.IsValid())
							{
								InParentWindow.Pin()->RequestDestroyWindow();
							}
							return FReply::Handled(); 
						})
					]
				]
			]
		];
	}

	bool WasOkPressed() const { return bOKPressed; }
protected:
	bool bOKPressed;
};

bool UEditorDialogLibrary::ShowObjectDetailsView(const FText& Title, UObject* InOutObject)
{	
	TArray<UObject*> ViewObjects = { InOutObject};
	return ShowObjectsDetailsView(Title, ViewObjects);
}

bool UEditorDialogLibrary::ShowObjectsDetailsView(const FText& Title, const TArray<UObject*>& InOutObjects)
{
	if (!FApp::IsUnattended() && !GIsRunningUnattendedScript)
	{
		TArray<UObject*> ViewObjects = InOutObjects;
		ViewObjects.Remove(nullptr);

		if (ViewObjects.Num())
		{
			TSharedRef<SWindow> Window = SNew(SWindow)
				.Title(Title)
				.SizingRule(ESizingRule::Autosized)
				.AutoCenter(EAutoCenter::PrimaryWorkArea)
				.SupportsMinimize(false)
				.SupportsMaximize(false);

			TSharedPtr<SObjParamDialog> Dialog;
			Window->SetContent(SAssignNew(Dialog, SObjParamDialog, Window, ViewObjects));
			GEditor->EditorAddModalWindow(Window);

			return Dialog->WasOkPressed();
		}
	}

	return false;
}


#undef LOCTEXT_NAMESPACE // "EditorDialogLibrary"