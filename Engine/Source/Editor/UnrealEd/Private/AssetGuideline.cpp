// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/AssetGuideline.h"

#if WITH_EDITOR


#include "Editor.h"
#include "ISettingsEditorModule.h"
#include "GameProjectGenerationModule.h"
#include "TimerManager.h"

#include "Interfaces/Interface_AssetUserData.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/IConsoleManager.h"
#include "Application/SlateApplicationBase.h"

#include "Misc/ConfigCacheIni.h"
#include "Engine/EngineTypes.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Docking/TabManager.h"

#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Notifications/INotificationWidget.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SHyperlink.h"

#define LOCTEXT_NAMESPACE "AssetGuideine"

class SAssetGuidelineNotification : public SCompoundWidget, public INotificationWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetGuidelineNotification){}
	
		SLATE_ATTRIBUTE( FText, TitleText)
		SLATE_ATTRIBUTE( FText, HyperlinkText )
		SLATE_EVENT(FSimpleDelegate, Hyperlink )
		SLATE_ATTRIBUTE( TArray<FNotificationButtonInfo>, ButtonDetails )

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		TitleText = InArgs._TitleText.Get();
		HyperlinkText = InArgs._HyperlinkText.Get();
		Hyperlink = InArgs._Hyperlink;
		
		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("NotificationList.ItemBackground"))
			[
				SNew(SBorder)
				.Padding( FMargin(5) )
				.BorderImage(FCoreStyle::Get().GetBrush("NotificationList.ItemBackground_Border"))
				.BorderBackgroundColor(FColor(0,0,0,1))
				[
					ConstructInternals(InArgs)
				]
			]
		];
	}


	/**
	 * Returns the internals of the notification
	 */
	TSharedRef<SHorizontalBox> ConstructInternals( const FArguments& InArgs ) 
	{
		TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

		// Notification image
		HorizontalBox->AddSlot()
		.AutoWidth()
		.Padding(10.f, 0.f, 0.f, 0.f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SNew(SImage)
			.Image(FCoreStyle::Get().GetBrush("NotificationList.DefaultMessage"))
		];

		{
			FSlateFontInfo Font = FCoreStyle::Get().GetFontStyle(TEXT("NotificationList.FontBold"));
			TSharedRef<SVerticalBox> TextAndInteractiveWidgetsBox = SNew(SVerticalBox);

			HorizontalBox->AddSlot()
			.AutoWidth()
			.Padding(10.f, 0.f, 15.f, 0.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				TextAndInteractiveWidgetsBox
			];

			// Build Text box
			TextAndInteractiveWidgetsBox->AddSlot()
			.AutoHeight()
			[
				SNew(SBox)
				[
					SNew(STextBlock)
					.Text(this, &SAssetGuidelineNotification::GetTextFromState)
					.Font(Font)
				]
			];

			TSharedRef<SVerticalBox> InteractiveWidgetsBox = SNew(SVerticalBox);
			TextAndInteractiveWidgetsBox->AddSlot()
			.AutoHeight()
			[
				InteractiveWidgetsBox
			];
			
			// Adds a hyperlink
			InteractiveWidgetsBox->AddSlot()
			.AutoHeight()
			.VAlign(VAlign_Bottom)
			.HAlign(HAlign_Right)
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.Visibility(this, &SAssetGuidelineNotification::GetInteractiveVisibility)
				[
					SNew(SHyperlink)
					.Text(this, &SAssetGuidelineNotification::GetHyperlinkTextFromState)
					.OnNavigate(this, &SAssetGuidelineNotification::OnHyperlinkClicked)
				]
			];

			// Adds any buttons that were passed in.
			{
				TSharedRef<SHorizontalBox> ButtonsBox = SNew(SHorizontalBox);
				for (int32 idx = 0; idx < InArgs._ButtonDetails.Get().Num(); idx++)
				{
					FNotificationButtonInfo Button = InArgs._ButtonDetails.Get()[idx];

					ButtonsBox->AddSlot()
					.AutoWidth()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.Text(Button.Text)
						.ToolTipText(Button.ToolTip)
						.OnClicked(this, &SAssetGuidelineNotification::OnButtonClicked, Button.Callback)
						.Visibility( this, &SAssetGuidelineNotification::GetInteractiveVisibility )
					];
				}
				InteractiveWidgetsBox->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				[
					ButtonsBox
				];
			}
		}

		// Build success/fail image
		HorizontalBox->AddSlot()
		.AutoWidth()
		[
			SNew(SBox)
			.Padding(FMargin(8.f, 0.f, 10.f, 0.f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Visibility( this, &SAssetGuidelineNotification::GetSuccessFailImageVisibility )
			[
				SNew(SImage)
				.Image( this, &SAssetGuidelineNotification::GetSuccessFailImage )
			]
		];

		return HorizontalBox;
	}

	/* Gets text based on current notification state*/
	FText GetTextFromState() const
	{
		switch (State)
		{
		case SNotificationItem::CS_Success: return LOCTEXT("RestartNeeded", "Plugins & project settings updated, but will be out of sync until restart.");
		case SNotificationItem::CS_Fail: return LOCTEXT("ChangeFailure", "Failed to change plugins & project settings.");
		}

		return TitleText;
	}

	/* Gets text based on current notification state*/
	FText GetHyperlinkTextFromState() const
	{
		// Make hyperlink text on sucess or fail empty, so that the box auto-resizes correctly.
		switch (State)
		{
		case SNotificationItem::CS_Success: return FText::GetEmpty();
		case SNotificationItem::CS_Fail: return FText::GetEmpty();
		}

		return HyperlinkText;
	}

	/* Used to determine whether interactive components are visible */
	EVisibility GetInteractiveVisibility() const
	{
		switch ( State )
		{
		case SNotificationItem::CS_None: return EVisibility::Visible;
		case SNotificationItem::CS_Pending: return EVisibility::Visible;
		case SNotificationItem::CS_Success: return EVisibility::Hidden;
		case SNotificationItem::CS_Fail: return EVisibility::Hidden;
		default:
			check( false );
			return EVisibility::Visible;
		}
	}

	/* Used as a wrapper for the callback, this means any code calling it does not require access to FReply type */
	FReply OnButtonClicked(FSimpleDelegate InCallback)
	{
		InCallback.ExecuteIfBound();
		return FReply::Handled();
	}

	/* Execute the delegate for the hyperlink, if bound */
	void OnHyperlinkClicked() const
	{
		Hyperlink.ExecuteIfBound();
	}

	EVisibility GetSuccessFailImageVisibility() const
	{
		return (State == SNotificationItem::ECompletionState::CS_Success || State == SNotificationItem::ECompletionState::CS_Fail) ? EVisibility::Visible : EVisibility::Collapsed;
	}

	const FSlateBrush* GetSuccessFailImage() const
	{
		return State == SNotificationItem::ECompletionState::CS_Success ? FCoreStyle::Get().GetBrush("NotificationList.SuccessImage") : FCoreStyle::Get().GetBrush("NotificationList.FailImage");
	}

	/** Begin INotificationWidget Interface */
	virtual void OnSetCompletionState(SNotificationItem::ECompletionState InState)
	{
		State = InState;
	}

	virtual TSharedRef< SWidget > AsWidget()
	{
		return AsShared();
	}
	/** End INotificationWidget Interface */

private:
	SNotificationItem::ECompletionState State;

	FText TitleText;

	FText HyperlinkText;
	FSimpleDelegate Hyperlink;
};

bool UAssetGuideline::IsPostLoadThreadSafe() const
{
	return true;
}

void UAssetGuideline::PostLoad()
{
	Super::PostLoad();

	// If we fail to package, this can trigger a re-build & load of failed assets 
	// via the UBT with 'WITH_EDITOR' on, but slate not initialized. Skip that.
	if (!FSlateApplicationBase::IsInitialized())
	{
		return;
	}

	static TArray<FName> TestedGuidelines;
	if (TestedGuidelines.Contains(GuidelineName))
	{
		return;
	}
	TestedGuidelines.AddUnique(GuidelineName);

	FString NeededPlugins;
	TArray<FString> IncorrectPlugins;
	for (const FString& Plugin : Plugins)
	{
		TSharedPtr<IPlugin> NeededPlugin = IPluginManager::Get().FindPlugin(Plugin);
		if (NeededPlugin.IsValid())
		{
			if (!NeededPlugin->IsEnabled())
			{
				NeededPlugins += NeededPlugin->GetFriendlyName() + "\n";
				IncorrectPlugins.Add(Plugin);
			}
		}
		else
		{
			NeededPlugins += Plugin + "\n";;
			IncorrectPlugins.Add(Plugin);
		}
	}

	FString NeededProjectSettings;
	TArray<FIniStringValue> IncorrectProjectSettings;
	for (const FIniStringValue& ProjectSetting : ProjectSettings)
	{
		if (IConsoleManager::Get().FindConsoleVariable(*ProjectSetting.Key))
		{
			FString CurrentIniValue;
			FString FilenamePath = FPaths::ProjectDir() + ProjectSetting.Filename;
			if (GConfig->GetString(*ProjectSetting.Section, *ProjectSetting.Key, CurrentIniValue, FilenamePath))
			{
				if (CurrentIniValue != ProjectSetting.Value)
				{
					NeededProjectSettings += FString::Printf(TEXT("[%s]  %s = %s\n"), *ProjectSetting.Section, *ProjectSetting.Key, *ProjectSetting.Value);
					IncorrectProjectSettings.Add(ProjectSetting);
				}
			}
			else
			{
				NeededProjectSettings += FString::Printf(TEXT("[%s]  %s = %s\n"), *ProjectSetting.Section, *ProjectSetting.Key, *ProjectSetting.Value);
				IncorrectProjectSettings.Add(ProjectSetting);
			}
		}
	}

	if (!NeededPlugins.IsEmpty() || !NeededProjectSettings.IsEmpty())
	{
		FText WarningHyperlinkText;
		FText NeededItems;
		{
			FText AssetName = FText::AsCultureInvariant(GetPackage() ? GetPackage()->GetFName().ToString() : GetFName().ToString());

			FText MissingPlugins = FText::Format(LOCTEXT("MissingPlugins", "Needed plugins: \n{0}"), NeededPlugins.IsEmpty() ? FText::GetEmpty() : FText::AsCultureInvariant(NeededPlugins));
			FText PluginWarning = FText::Format(LOCTEXT("PluginWarning", "Asset '{0}' needs the above plugins. Assets related to '{0}' may not display properly.\n	Attemping to save '{0}' or related assets may result in irreverisble modification due to missing plugins. \n"), AssetName);

			FText MissingProjectSettings = FText::Format(LOCTEXT("MissingProjectSettings", "Needed project settings: \n{0}"), NeededProjectSettings.IsEmpty() ? FText::GetEmpty() : FText::AsCultureInvariant(NeededProjectSettings));
			FText ProjectSettingWarning = FText::Format(LOCTEXT("ProjectSettingWarning", "Asset '{0}' needs the above project settings. Assets related to '{0}' may not display properly."), AssetName);

			FFormatNamedArguments WarningHyperlinkArgs;
			WarningHyperlinkArgs.Add("PluginHyperlink", NeededPlugins.IsEmpty() ? FText::GetEmpty() : FText::Format(FText::AsCultureInvariant("{0}{1}\n"), MissingPlugins, PluginWarning));
			WarningHyperlinkArgs.Add("ProjectSettingHyperlink", NeededProjectSettings.IsEmpty() ? FText::GetEmpty() : FText::Format(FText::AsCultureInvariant("{0}{1}\n"), MissingProjectSettings, ProjectSettingWarning));
			WarningHyperlinkText = FText::Format(LOCTEXT("WarningHyperLink", "{PluginHyperlink}{ProjectSettingHyperlink}"), WarningHyperlinkArgs);

			FText NeedPlugins = LOCTEXT("NeedPlugins", "Missing Plugins!");
			FText NeedProjectSettings = LOCTEXT("NeedProjectSettings", "Missing Project Settings!");
			FText NeedBothGuidelines = LOCTEXT("NeedBothGuidelines", "Missing Plugins & Project Settings!");
			NeededItems = !NeededPlugins.IsEmpty() && !NeededProjectSettings.IsEmpty() ? NeedBothGuidelines : !NeededPlugins.IsEmpty() ? NeedPlugins : NeedProjectSettings;
		}

		auto WarningHyperLink = [](bool NeedPluginLink, bool NeedProjectSettingLink)
		{
			if (NeedProjectSettingLink)
			{
				FGlobalTabmanager::Get()->TryInvokeTab(FName("ProjectSettings"));
			}

			if (NeedPluginLink)
			{
				FGlobalTabmanager::Get()->TryInvokeTab(FName("PluginsEditor"));
			}
		};

		FNotificationInfo Info(NeededItems);
		Info.bFireAndForget = false;
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("GuidelineEnableMissing", "Enable Missing..."), 
			LOCTEXT("GuidelineEnableMissingTT", "Attempt to automatically set missing plugins / project settings"), 
			FSimpleDelegate::CreateUObject(this, &UAssetGuideline::EnableMissingGuidelines, IncorrectPlugins, IncorrectProjectSettings)));
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("GuidelineDismiss", "Dismiss"),
			LOCTEXT("GuidelineDismissTT", "Dismiss this notification."), 
			FSimpleDelegate::CreateUObject(this, &UAssetGuideline::DismissNotifications)));
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("GuidelineRemove", "Remove guideline from asset"),
			LOCTEXT("GuidelineRemoveTT", "Remove asset guideline. Preventing this notifcation from showing up again."),
			FSimpleDelegate::CreateUObject(this, &UAssetGuideline::RemoveAssetGuideline)));
		Info.ContentWidget = SNew(SAssetGuidelineNotification)
			.TitleText(NeededItems)
			.HyperlinkText(WarningHyperlinkText)
			.Hyperlink(FSimpleDelegate::CreateLambda(WarningHyperLink, !NeededPlugins.IsEmpty(), !NeededProjectSettings.IsEmpty()))
			.ButtonDetails(Info.ButtonDetails);

		NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
		if (TSharedPtr<SNotificationItem> NotificationPin = NotificationPtr.Pin())
		{
			NotificationPin->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}
}

void UAssetGuideline::BeginDestroy()
{
	DismissNotifications();

	Super::BeginDestroy();
}

void UAssetGuideline::EnableMissingGuidelines(TArray<FString> IncorrectPlugins, TArray<FIniStringValue> IncorrectProjectSettings)
{
	if (TSharedPtr<SNotificationItem> NotificationPin = NotificationPtr.Pin())
	{
		bool bSuccess = true;

		if (IncorrectPlugins.Num() > 0)
		{
			FGameProjectGenerationModule::Get().TryMakeProjectFileWriteable(FPaths::GetProjectFilePath());
			bSuccess = !FPlatformFileManager::Get().GetPlatformFile().IsReadOnly(*FPaths::GetProjectFilePath());
		}

		if (bSuccess)
		{
			for (const FString& IncorrectPlugin : IncorrectPlugins)
			{
				FText FailMessage;
				bool bPluginEnabled = IProjectManager::Get().SetPluginEnabled(IncorrectPlugin, true, FailMessage);

				if (bPluginEnabled && IProjectManager::Get().IsCurrentProjectDirty())
				{
					bPluginEnabled = IProjectManager::Get().SaveCurrentProjectToDisk(FailMessage);
				}

				if (!bPluginEnabled)
				{
					bSuccess = false;
					break;
				}
			}
		}

		for (const FIniStringValue& IncorrectProjectSetting : IncorrectProjectSettings)
		{
			// Only fails if file DNE
			FString FilenamePath = FPaths::ProjectDir() + IncorrectProjectSetting.Filename;
			if (bSuccess && GConfig->Find(FilenamePath, false /* CreateIfFound */))
			{
				FGameProjectGenerationModule::Get().TryMakeProjectFileWriteable(FilenamePath);

				if (!FPlatformFileManager::Get().GetPlatformFile().IsReadOnly(*FilenamePath))
				{
					GConfig->SetString(*IncorrectProjectSetting.Section, *IncorrectProjectSetting.Key, *IncorrectProjectSetting.Value, FilenamePath);
				}
				else
				{
					bSuccess = false;
					break;
				}
			}
			else
			{
				bSuccess = false;
				break;
			}
		}

		if (bSuccess)
		{
			auto ShowRestartPrompt = []()
			{
				FModuleManager::GetModuleChecked<ISettingsEditorModule>("SettingsEditor").OnApplicationRestartRequired();
			};

			FTimerHandle NotificationFadeTimer;
			GEditor->GetTimerManager()->SetTimer(NotificationFadeTimer, FTimerDelegate::CreateLambda(ShowRestartPrompt), 3.0f, false);
		}

		NotificationPin->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		NotificationPin->ExpireAndFadeout();
		NotificationPtr.Reset();
	}
}

void UAssetGuideline::DismissNotifications()
{
	if (TSharedPtr<SNotificationItem> NotificationPin = NotificationPtr.Pin())
	{
		NotificationPin->SetCompletionState(SNotificationItem::CS_None);
		NotificationPin->ExpireAndFadeout();
		NotificationPtr.Reset();
	}
}

void UAssetGuideline::RemoveAssetGuideline()
{
	if (TSharedPtr<SNotificationItem> NotificationPin = NotificationPtr.Pin())
	{
		if (IInterface_AssetUserData* UserDataOuter = Cast<IInterface_AssetUserData>(GetOuter()))
		{
			UserDataOuter->RemoveUserDataOfClass(UAssetGuideline::StaticClass());
			GetOuter()->MarkPackageDirty();
		}
		NotificationPin->SetCompletionState(SNotificationItem::CS_None);
		NotificationPin->ExpireAndFadeout();
		NotificationPtr.Reset();
	}
}

#undef LOCTEXT_NAMESPACE 

#endif // WITH_EDITOR