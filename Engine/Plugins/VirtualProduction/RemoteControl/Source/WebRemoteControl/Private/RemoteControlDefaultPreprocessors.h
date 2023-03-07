// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "Misc/WildcardString.h"
#include "RemoteControlSettings.h"
#include "WebRemoteControlInternalUtils.h"

#if WITH_EDITOR
#include "Dialogs/Dialogs.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformFileManager.h"
#include "ISourceControlModule.h"
#include "Misc/MessageDialog.h"
#include "SSettingsEditorCheckoutNotice.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#endif

#define LOCTEXT_NAMESPACE "WebRemoteControl"

namespace UE::WebRemoteControl
{
	enum class EPreprocessorResult : uint8
	{
		RequestPassthrough,
		RequestHandled
	};
	
	using FRCPreprocessorHandler = TFunction<EPreprocessorResult(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)>;

	/** Utility function to wrap a preprocessor handler to a http request handler than the HttpRouter can take. */
	FHttpRequestHandler MakeHttpRequestHandler(FRCPreprocessorHandler Handler)
	{
		return [WrappedHandler = MoveTemp(Handler)](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			return WrappedHandler(Request, OnComplete) == EPreprocessorResult::RequestPassthrough ? false : true;
		};
	}

#if WITH_EDITOR
	TWeakPtr<SNotificationItem> NoPassphrasePrompt;
	FText NoActionTakenText = LOCTEXT("NoActionTaken", "No action was taken, further requests from this IP will be denied.");

	const FString& GetRemoteControlConfigPath()
	{
		static const FString RemoteControlConfigPath = FPackageName::LongPackageNameToFilename(GetDefault<URemoteControlSettings>()->GetPackage()->GetName(), TEXT(".ini"));
		return RemoteControlConfigPath;
	}
	void SaveRemoteControlConfig()
	{
		FString ConfigFilename = FPaths::ConvertRelativePathToFull(URemoteControlSettings::StaticClass()->GetConfigName());
		
		if (!FPlatformFileManager::Get().GetPlatformFile().IsReadOnly(*ConfigFilename))
		{
			GetMutableDefault<URemoteControlSettings>()->SaveConfig();
		}
		else
		{
			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*ConfigFilename))
			{
				if (ISourceControlModule::Get().IsEnabled())
				{
					if (SettingsHelpers::IsSourceControlled(ConfigFilename))
					{
						FText DisplayMessage = LOCTEXT("SaveConfigCheckoutMessage", "The configuration file for these settings is currently not checked out. Would you like to check it out from revision control?");
						if (FMessageDialog::Open(EAppMsgType::YesNo, DisplayMessage) == EAppReturnType::Yes)
						{
							constexpr bool bForceSourceControlUpdate = true;
							constexpr bool bShowErrorInNotification = true;
							SettingsHelpers::CheckOutOrAddFile(ConfigFilename, bForceSourceControlUpdate, bShowErrorInNotification);
							GetMutableDefault<URemoteControlSettings>()->SaveConfig();
							return;
						}
					}
				}

				FText DisplayMessage = LOCTEXT("MakeConfigWritable", "The configuration file for these settings is currently read only. Would you like to make it writable?");
				if (FMessageDialog::Open(EAppMsgType::YesNo, DisplayMessage) == EAppReturnType::Yes)
				{
					if (FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ConfigFilename, false))
					{
						GetMutableDefault<URemoteControlSettings>()->SaveConfig();
					}
					else
					{
						FText ErrorMessage = FText::Format(LOCTEXT("FailedToMakeWritable", "Could not make {0} writable."), FText::FromString(ConfigFilename));;

						FNotificationInfo MakeWritableNotification(MoveTemp(ErrorMessage));
						MakeWritableNotification.ExpireDuration = 3.0f;
						FSlateNotificationManager::Get().AddNotification(MakeWritableNotification);
					}
				}
			}
		}
	}

	void AddIPToAllowlist(FString IPAddress)
	{
		if (TSharedPtr<SNotificationItem> Notification = NoPassphrasePrompt.Pin())
		{
			Notification->SetCompletionState(SNotificationItem::ECompletionState::CS_Pending);

			GetMutableDefault<URemoteControlSettings>()->AllowedIPsForRemotePassphrases.Add(IPAddress);
			SaveRemoteControlConfig();
			
			Notification->SetCompletionState(SNotificationItem::ECompletionState::CS_Success);
			Notification->ExpireAndFadeout();
		}
	}

	void CreatePassphrase()
	{
		if (TSharedPtr<SNotificationItem> Notification = NoPassphrasePrompt.Pin())
		{
			Notification->SetCompletionState(SNotificationItem::ECompletionState::CS_Pending);

			TSharedPtr<SEditableTextBox> PassphraseIdentifierTextbox;
			TSharedPtr<SEditableTextBox> PassphraseTextbox;

			TSharedPtr<SWindow> Window = SNew(SWindow)
				.Title(LOCTEXT("RemoteControlCreatePassphrase", "Create Passphrase"))
				.SizingRule(ESizingRule::Autosized)
				.SupportsMaximize(false)
				.SupportsMinimize(false)
				.FocusWhenFirstShown(true);

			auto OnPassphraseEntered = [Window, Notification](const FText& PassphraseIdentifier, const FText& PassphraseText)
			{
				FRCPassphrase PassphraseStruct;
				PassphraseStruct.Identifier = PassphraseIdentifier.ToString();
				PassphraseStruct.Passphrase = FMD5::HashAnsiString(*PassphraseText.ToString());
				GetMutableDefault<URemoteControlSettings>()->Passphrases.Add(MoveTemp(PassphraseStruct));
				SaveRemoteControlConfig();

				Notification->SetCompletionState(SNotificationItem::ECompletionState::CS_Success);
				Notification->ExpireAndFadeout();

				if (Window)
				{
					Window->RequestDestroyWindow();
				}
			};

			Window->SetContent(
				SNew(SBorder)
				.Padding(4.f)
				.VAlign(VAlign_Center)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.Padding(0.f, 5.f)
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CreatePassphraseDescription", "Create a passphrase for the client to use."))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(PassphraseIdentifierTextbox, SEditableTextBox)
						.HintText(LOCTEXT("PassphraseIdentifier", "Passphrase Identifier (Optional)"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(PassphraseTextbox, SEditableTextBox)
						.HintText(LOCTEXT("Passphrase", "Passphrase"))
						.OnTextCommitted_Lambda([PassphraseIdentifierTextbox, OnPassphraseEntered](const FText& InText, ETextCommit::Type CommitType)
						{
							if (PassphraseIdentifierTextbox && !InText.IsEmpty() && CommitType == ETextCommit::OnEnter)
							{
								OnPassphraseEntered(PassphraseIdentifierTextbox->GetText(), InText);
							}
						})
					]
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Right)
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("OK", "OK"))
						.OnClicked_Lambda([PassphraseTextbox, PassphraseIdentifierTextbox, OnPassphraseEntered]()
						{
							if (PassphraseIdentifierTextbox && PassphraseTextbox && !PassphraseTextbox->GetText().IsEmpty())
							{
								OnPassphraseEntered(PassphraseIdentifierTextbox->GetText(), PassphraseTextbox->GetText());
							}

							return FReply::Handled();
						})
					]
				]
			);

			Window->GetOnWindowClosedEvent().AddLambda([Notification](const TSharedRef<SWindow>&)
			{
				if (Notification->GetCompletionState() == SNotificationItem::ECompletionState::CS_Pending)
				{
					Notification->SetText(NoActionTakenText);
					Notification->SetSubText(FText::GetEmpty());
					Notification->SetCompletionState(SNotificationItem::ECompletionState::CS_Fail);
					Notification->ExpireAndFadeout();
				}
			});

			Window->GetOnWindowActivatedEvent().AddLambda([PassphraseTextbox]()
			{
				if (PassphraseTextbox)
				{
					constexpr uint32 UserIndex = 0;
					FSlateApplication::Get().SetUserFocus(UserIndex, PassphraseTextbox, EFocusCause::SetDirectly);
				}
			});

			GEditor->EditorAddModalWindow(Window.ToSharedRef());
		}
	}

	void DisableRemotePassphrases()
	{
		if (TSharedPtr<SNotificationItem> Notification = NoPassphrasePrompt.Pin())
		{
			Notification->SetCompletionState(SNotificationItem::ECompletionState::CS_Pending);
			
			if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("DisableRemotePassphrases", "Are you sure you want to disable passphrase requirement for remote clients? This will allow anyone on your network to access the remote control servers and could open you up to vulnerabilities.")) == EAppReturnType::Yes)
			{
				GetMutableDefault<URemoteControlSettings>()->bEnforcePassphraseForRemoteClients = false;
				SaveRemoteControlConfig();
				
				Notification->SetCompletionState(SNotificationItem::ECompletionState::CS_Success);
                Notification->ExpireAndFadeout();
			}
			else
			{
				Notification->SetText(NoActionTakenText);
				Notification->SetSubText(FText::GetEmpty());
				Notification->SetCompletionState(SNotificationItem::ECompletionState::CS_Fail);
				Notification->ExpireAndFadeout();
			}
		}
	}
#endif

	// Notifies the editor if a client tries to access the RC server without a passphrase.
	EPreprocessorResult RemotePassphraseEnforcementPreprocessor(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (GetDefault<URemoteControlSettings>()->bRestrictServerAccess)
		{
			if (Request.PeerAddress)
			{
				constexpr bool bAppendPort = false;
				const FString PeerAddress = Request.PeerAddress->ToString(bAppendPort);

				if (!GetDefault<URemoteControlSettings>()->bEnforcePassphraseForRemoteClients)
				{
					return EPreprocessorResult::RequestPassthrough;
				}

				if (PeerAddress.Contains(TEXT("127.0.0.1")) || PeerAddress.Contains(TEXT("localhost")))
				{
					if (const TArray<FString>* ForwardedIP = Request.Headers.Find(WebRemoteControlInternalUtils::ForwardedIPHeader))
					{
						if (ForwardedIP->Num())
						{
							// We will need to rework this when the IP range change is in.
							if (GetDefault<URemoteControlSettings>()->AllowedIPsForRemotePassphrases.Contains(ForwardedIP->Last()))
							{
								return EPreprocessorResult::RequestPassthrough;
							}
						}
						else
						{
							return EPreprocessorResult::RequestPassthrough;
						}
					}
					else
					{
						return EPreprocessorResult::RequestPassthrough;
					}
				}

				if (GetDefault<URemoteControlSettings>()->AllowedIPsForRemotePassphrases.Contains(PeerAddress))
				{
					return EPreprocessorResult::RequestPassthrough;
				}

				if (GetDefault<URemoteControlSettings>()->Passphrases.Num())
				{
					const TArray<FString>* PassphraseHeader = Request.Headers.Find(WebRemoteControlInternalUtils::PassphraseHeader);
					if (!PassphraseHeader || PassphraseHeader->Num() == 0)
					{
						TUniquePtr<FHttpServerResponse> Response = WebRemoteControlInternalUtils::CreateHttpResponse();

						const FString ErrorMessage = TEXT("Remote passphrase enforcement is enabled but no passphrase was specified!");
						UE_LOG(LogRemoteControl, Error, TEXT("%s"), *ErrorMessage);
						IRemoteControlModule::BroadcastError(ErrorMessage);
						WebRemoteControlInternalUtils::CreateUTF8ErrorMessage(ErrorMessage, Response->Body);

						Response->Code = EHttpServerResponseCodes::Denied;
						OnComplete(MoveTemp(Response));
						return EPreprocessorResult::RequestHandled;
					}

					if (!WebRemoteControlInternalUtils::CheckPassphrase(PassphraseHeader->Last()))
					{
						OnComplete(WebRemoteControlInternalUtils::CreatedInvalidPassphraseResponse());
						return EPreprocessorResult::RequestHandled;
					}
					else
					{
						return EPreprocessorResult::RequestPassthrough;
					}
				}
				else
				{
					/* Prompt user, checkout settings if needed,
					 * Either
					 * 1: Explicitely add peer IP to Allowlist
					 * 2: Create a passphrase, tell user he must enter that on the app he's using or put it in the header
					 * 3: Disable remote passphrase enforcement. (Warn that this is dangerous)
					 */
	#if WITH_EDITOR
					if (GEditor)
					{
						if (!NoPassphrasePrompt.IsValid() || NoPassphrasePrompt.Pin()->GetCompletionState() == SNotificationItem::ECompletionState::CS_None)
						{
							FNotificationInfo Info(LOCTEXT("NoPassphraseNotificationHeader", "Remote control request denied!"));
							Info.SubText = FText::Format(LOCTEXT("NoPassphraseNotificationSubtext", "A remote control request was made by an external client ({0}) without a passphrase."), FText::FromString(PeerAddress));
							Info.bFireAndForget = false;
							Info.FadeInDuration = 0.5f;
							Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("AllowListIP", "AllowList IP"), LOCTEXT("AllowListTooltip", "Add this IP to the list of allowed IPs that can make remote control requests without a passphrase."), FSimpleDelegate::CreateStatic(&AddIPToAllowlist, PeerAddress), SNotificationItem::CS_None));
							Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("CreatePassphrase", "Create Passphrase"), LOCTEXT("CreatePassPhraseTooltip", "Create a passphrase for this client to use."), FSimpleDelegate::CreateStatic(&CreatePassphrase), SNotificationItem::CS_None));
							Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("DisableRemotePassphrase", "Disable Passphrases"), LOCTEXT("DisableRemotePassphrasesTooltip", "Disable the requirement for remote control requests coming from external clients to have a passphrase.\nWarning: This should only be done as a last resort since all clients on the network will be able to access your servers."), FSimpleDelegate::CreateStatic(&DisableRemotePassphrases), SNotificationItem::CS_None));
							Info.WidthOverride = 450.f;
							NoPassphrasePrompt = FSlateNotificationManager::Get().AddNotification(MoveTemp(Info));	
						}
					}
	#endif
					// In -game or packaged, we won't prompt the user so just deny the request.
					const FString ErrorMessage = TEXT("A passphrase for remote control is required but we can't create one in -game or packaged.");
					UE_LOG(LogRemoteControl, Error, TEXT("%s"), *ErrorMessage);
					IRemoteControlModule::BroadcastError(ErrorMessage);

					OnComplete(WebRemoteControlInternalUtils::CreatedInvalidPassphraseResponse());
					return EPreprocessorResult::RequestHandled;
				}
			}
		}

		return EPreprocessorResult::RequestPassthrough;
	}
	
	EPreprocessorResult PassphrasePreprocessor(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TUniquePtr<FHttpServerResponse> Response = WebRemoteControlInternalUtils::CreateHttpResponse();

		TArray<FString> ValueArray = {};
		if (Request.Headers.Find(WebRemoteControlInternalUtils::PassphraseHeader))
		{
			ValueArray = Request.Headers[WebRemoteControlInternalUtils::PassphraseHeader];
		}
		
		const FString Passphrase = !ValueArray.IsEmpty() ? ValueArray.Last() : "";

		if (!WebRemoteControlInternalUtils::CheckPassphrase(Passphrase))
		{
			OnComplete(WebRemoteControlInternalUtils::CreatedInvalidPassphraseResponse());
			return EPreprocessorResult::RequestHandled;
		}

		return EPreprocessorResult::RequestPassthrough;
	}

	EPreprocessorResult IPValidationPreprocessor(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (GetDefault<URemoteControlSettings>()->bRestrictServerAccess)
		{
			TUniquePtr<FHttpServerResponse> Response = WebRemoteControlInternalUtils::CreateHttpResponse();

			FString OriginHeader;
			if (const TArray<FString>* OriginHeaders = Request.Headers.Find(WebRemoteControlInternalUtils::OriginHeader))
			{
				if (OriginHeaders->Num())
				{
					OriginHeader = (*OriginHeaders)[0];
				}
			}

			OriginHeader.RemoveSpacesInline();
			OriginHeader.TrimStartAndEndInline();
		
			auto SimplifyAddress = [] (FString Address)
			{
				Address.RemoveFromStart(TEXT("https://www."));
				Address.RemoveFromStart(TEXT("http://www."));
				Address.RemoveFromStart(TEXT("https://"));
				Address.RemoveFromStart(TEXT("http://"));
				Address.RemoveFromEnd(TEXT("/"));
				return Address;
			};

			const FString SimplifiedOrigin = SimplifyAddress(OriginHeader);
			const FWildcardString SimplifiedAllowedOrigin = SimplifyAddress(GetDefault<URemoteControlSettings>()->AllowedOrigin);
			if (!SimplifiedOrigin.IsEmpty() && GetDefault<URemoteControlSettings>()->AllowedOrigin != TEXT("*"))
			{
				if (!SimplifiedAllowedOrigin.IsMatch(SimplifiedOrigin))
				{
					WebRemoteControlInternalUtils::CreateUTF8ErrorMessage(FString::Printf(TEXT("Client origin %s does not respect the allowed origin set in Remote Control Settings."), *OriginHeader), Response->Body);
					Response->Code = EHttpServerResponseCodes::Denied;
					OnComplete(MoveTemp(Response));
					return EPreprocessorResult::RequestHandled;
				}
			}

			if (Request.PeerAddress)
			{
				constexpr bool bAppendPort = false;
				FString ClientIP = Request.PeerAddress->ToString(bAppendPort);
				const FWildcardString WildcardAllowedIP = SimplifyAddress(GetDefault<URemoteControlSettings>()->AllowedIP);
				
				// Allow requests from localhost
				if (ClientIP != TEXT("localhost") && ClientIP != TEXT("127.0.0.1"))
				{
					if (!WildcardAllowedIP.IsEmpty() && WildcardAllowedIP.IsMatch(ClientIP))
					{
						WebRemoteControlInternalUtils::CreateUTF8ErrorMessage(FString::Printf(TEXT("Client IP %s does not respect the allowed IP set in Remote Control Settings."), *ClientIP), Response->Body);
						Response->Code = EHttpServerResponseCodes::Denied;
						OnComplete(MoveTemp(Response));
						return EPreprocessorResult::RequestHandled;
					}
				}
			}
		}

		return EPreprocessorResult::RequestPassthrough;
	}
}

#undef LOCTEXT_NAMESPACE /* WebRemoteControl */