// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Notifications/SlateAsyncTaskNotificationImpl.h"

#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Notifications/INotificationWidget.h"
#include "Framework/Notifications/NotificationManager.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"

#include "Misc/App.h"
#include "Misc/ScopeLock.h"
#include "Templates/Atomic.h"

#define LOCTEXT_NAMESPACE "SlateAsyncTaskNotification"

class SSlateAsyncTaskNotificationWidget : public SCompoundWidget, public INotificationWidget
{
public:
	SLATE_BEGIN_ARGS(SSlateAsyncTaskNotificationWidget) {}
	SLATE_END_ARGS()

	//~ SWidget
	void Construct(const FArguments& InArgs, const FAsyncTaskNotificationConfig& InConfig);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	//~ INotificationWidget
	virtual void OnSetCompletionState(SNotificationItem::ECompletionState State) override;
	virtual TSharedRef<SWidget> AsWidget() override;

	/** Set the notification item that owns this widget */
	void SetOwner(TSharedPtr<SNotificationItem> InOwningNotification);

	/** Update the notification state */
	void UpdateNotification(const FText& InTitleText, const FText& InProgressText, const FText& InPromptText, const FSimpleDelegate& InHyperlink, const FText& InHyperlinkText);

	/** Set the pending completion state of the notification (applied during the next Tick) and reset the external UI reference */
	void SetPendingCompletionState(const EAsyncTaskNotificationState InPendingCompletionState, TSharedPtr<SSlateAsyncTaskNotificationWidget>* ExternalReferenceToReset);

	/** Set whether this task be canceled */
	void SetCanCancel(const TAttribute<bool>& InCanCancel);

	/** Set whether to keep this notification open on success */
	void SetKeepOpenOnSuccess(const TAttribute<bool>& InKeepOpenOnSuccess);

	/** Set whether to keep this notification open on failure */
	void SetKeepOpenOnFailure(const TAttribute<bool>& InKeepOpenOnFailure);

	EAsyncTaskNotificationPromptAction GetPromptAction() const;
private:
	/** Sync attribute bindings with the cached values (once per-frame from the game thread) */
	void SyncAttributes();

	/** UI Text */
	EVisibility GetTitleTextVisibility() const;
	FText GetTitleText() const;
	EVisibility GetProgressTextVisibility() const;
	FText GetProgressText() const;

	/** Throbber */
	EVisibility GetThrobberVisibility() const;

	/** Status Icon */
	EVisibility GetStatusIconVisibility() const;
	const FSlateBrush* GetStatusIconBrush() const;

	/** Cancel button */
	bool IsCancelButtonEnabled() const;
	EVisibility GetCancelButtonVisibility() const;
	FReply OnCancelButtonClicked();

	/** Prompt button */
	bool IsPromptButtonEnabled() const;
	EVisibility GetPromptButtonVisibility() const;
	FReply OnPromptButtonClicked();
	FText GetPromptButtonText() const;

	/** Close button */
	EVisibility GetCloseButtonVisibility() const;
	FReply OnCloseButtonClicked();

	/** Hyperlink */
	void OnHyperlinkClicked() const;
	FText GetHyperlinkText() const;
	EVisibility GetHyperlinkVisibility() const;

	/** Get the current completion state from the parent notification */
	SNotificationItem::ECompletionState GetNotificationCompletionState() const;

	/** Action taken for the task, resets to none on notification state change. */
	TAtomic<EAsyncTaskNotificationPromptAction> PromptAction;

	/** Can this task be canceled? Will show a cancel button for in-progress tasks */
	TAttribute<bool> bCanCancelAttr = false;
	bool bCanCancel = false;

	/** Keep this notification open on success? Will show a close button */
	TAttribute<bool> bKeepOpenOnSuccessAttr = false;
	bool bKeepOpenOnSuccess = false;

	/** Keep this notification open on failure? Will show an close button */
	TAttribute<bool> bKeepOpenOnFailureAttr = false;
	bool bKeepOpenOnFailure = false;

	/** The title text displayed in the notification (if any) */
	FText TitleText;

	/** The progress text displayed in the notification (if any) */
	FText ProgressText;

	/** The prompt text displayed on the prompt button in the notification (if any)*/
	FText PromptText;

	/** When set this will display as a hyperlink on the right side of the notification. */
	FSimpleDelegate Hyperlink;

	/** Text to display for the hyperlink message */
	FText HyperlinkText;

	/** The pending completion state of the notification (if any, applied during the next Tick) */
	TOptional<EAsyncTaskNotificationState> PendingCompletionState;

	/** The current completion state of this widget. */
	EAsyncTaskNotificationState NotificationState = EAsyncTaskNotificationState::None;

	/** Pointer to the notification item that owns this widget (this is a deliberate reference cycle as we need this object alive until we choose to expire it, at which point we release our reference to allow everything to be destroyed) */
	TSharedPtr<SNotificationItem> OwningNotification;

	/** Critical section preventing concurrent access to the attributes */
	FCriticalSection AttributesCS;

	/** Critical section preventing the game thread from completing this widget while another thread is in the progress of setting the completion state and cleaning up its UI references */
	FCriticalSection CompletionCS;
};

void SSlateAsyncTaskNotificationWidget::Construct(const FArguments& InArgs, const FAsyncTaskNotificationConfig& InConfig)
{
	PromptAction = FApp::IsUnattended() ? EAsyncTaskNotificationPromptAction::Unattended : EAsyncTaskNotificationPromptAction::None;
	bCanCancelAttr = InConfig.bCanCancel;
	bKeepOpenOnSuccessAttr = InConfig.bKeepOpenOnSuccess;
	bKeepOpenOnFailureAttr = InConfig.bKeepOpenOnFailure;
	SyncAttributes();

	ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(15.0f))
		.BorderImage(FCoreStyle::Get().GetBrush("NotificationList.ItemBackground"))
		[
			SNew(SHorizontalBox)

			// Main Icon
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(SImage)
				.Image(InConfig.Icon ? InConfig.Icon : FCoreStyle::Get().GetBrush(TEXT("NotificationList.DefaultMessage")))
			]

			// Text
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)

				// Title Text
				+SVerticalBox::Slot()
				.HAlign(HAlign_Left)
				[
					SNew(STextBlock)
					.Margin(FMargin(10.0f, 0.0f, 0.0f, 0.0f))
					.Font(FCoreStyle::Get().GetFontStyle(TEXT("NotificationList.FontBold")))
					.Text(this, &SSlateAsyncTaskNotificationWidget::GetTitleText)
					.Visibility(this, &SSlateAsyncTaskNotificationWidget::GetTitleTextVisibility)
				]

				// Progress Text
				+SVerticalBox::Slot()
				.HAlign(HAlign_Left)
				[
					SNew(STextBlock)
					.Margin(FMargin(10.0f, 5.0f, 0.0f, 0.0f))
					.Font(FCoreStyle::Get().GetFontStyle(TEXT("NotificationList.FontLight")))
					.Text(this, &SSlateAsyncTaskNotificationWidget::GetProgressText)
					.Visibility(this, &SSlateAsyncTaskNotificationWidget::GetProgressTextVisibility)
				]
			]

			// Throbber/Status Icon + Buttons
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(15.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SVerticalBox)

				// Throbber/Status Icon
				+SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)

					// Throbber
					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.Padding(FMargin(5.0f, 0.0f, 10.0f, 0.0f))
						.Visibility(this, &SSlateAsyncTaskNotificationWidget::GetThrobberVisibility)
						[
							SNew(SThrobber)
						]
					]

					// Status Icon
					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.Padding(FMargin(8.0f, 0.0f, 10.0f, 0.0f))
						.Visibility(this, &SSlateAsyncTaskNotificationWidget::GetStatusIconVisibility)
						[
							SNew(SImage)
							.Image(this, &SSlateAsyncTaskNotificationWidget::GetStatusIconBrush)
						]
					]
				]

				// Hyperlink
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox)
					.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					.Visibility(this, &SSlateAsyncTaskNotificationWidget::GetHyperlinkVisibility)
					[
						SNew(SHyperlink)
						.Text(this, &SSlateAsyncTaskNotificationWidget::GetHyperlinkText)
						.OnNavigate(this, &SSlateAsyncTaskNotificationWidget::OnHyperlinkClicked)
					]
				]

				// Buttons
				+SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(0.0f, 5.0f, 0.0f, 0.0f))
				[
					SNew(SHorizontalBox)

					// Prompt Button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(5.0f, 0.0f, 5.0f, 0.0f))
					[
						SNew(SButton)
						.Text_Raw(this, &SSlateAsyncTaskNotificationWidget::GetPromptButtonText)
						.Visibility(this, &SSlateAsyncTaskNotificationWidget::GetPromptButtonVisibility)
						.OnClicked(this, &SSlateAsyncTaskNotificationWidget::OnPromptButtonClicked)
					]

					// Cancel Button
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("CancelButton", "Cancel"))
						.Visibility(this, &SSlateAsyncTaskNotificationWidget::GetCancelButtonVisibility)
						.OnClicked(this, &SSlateAsyncTaskNotificationWidget::OnCancelButtonClicked)
					]

					// Close Button
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("CloseButton", "Close"))
						.Visibility(this, &SSlateAsyncTaskNotificationWidget::GetCloseButtonVisibility)
						.OnClicked(this, &SSlateAsyncTaskNotificationWidget::OnCloseButtonClicked)
					]
				]
			]
		]
	];
}

void SSlateAsyncTaskNotificationWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SyncAttributes();

	EAsyncTaskNotificationState CompletionStateToApply = EAsyncTaskNotificationState::None;
	{
		FScopeLock Lock(&CompletionCS);

		if (PendingCompletionState.IsSet())
		{
			CompletionStateToApply = PendingCompletionState.GetValue();
			PendingCompletionState.Reset();
		}
	}

	if (OwningNotification && CompletionStateToApply != EAsyncTaskNotificationState::None)
	{
		NotificationState = CompletionStateToApply;
		SNotificationItem::ECompletionState OwningCompletionState = SNotificationItem::CS_None;
		switch (NotificationState)
		{
		case EAsyncTaskNotificationState::Pending:
			OwningCompletionState = SNotificationItem::CS_Pending;
			break;
		case EAsyncTaskNotificationState::Failure:
			OwningCompletionState = SNotificationItem::CS_Fail;
			break;
		case EAsyncTaskNotificationState::Success:
			OwningCompletionState = SNotificationItem::CS_Success;
			break;
		case EAsyncTaskNotificationState::Prompt:
			OwningNotification->Pulse(FLinearColor(0.f, 0.f, 1.f));

			break;
		}
		if (OwningCompletionState != SNotificationItem::CS_None && OwningCompletionState != OwningNotification->GetCompletionState())
		{
			OwningNotification->SetCompletionState(OwningCompletionState);
		}
	}
}

void SSlateAsyncTaskNotificationWidget::OnSetCompletionState(SNotificationItem::ECompletionState State)
{
	check(State == GetNotificationCompletionState());

	// If we completed and we aren't keeping the notification open (which will show the Close button), then expire the notification immediately
	if ((State == SNotificationItem::CS_Success || State == SNotificationItem::CS_Fail) && GetCloseButtonVisibility() == EVisibility::Collapsed)
	{
		if (OwningNotification)
		{
			// Perform the normal automatic fadeout
			OwningNotification->ExpireAndFadeout();

			// Release our reference to our owner so that everything can be destroyed
			OwningNotification.Reset();
		}
	}

	// Reset the `PromptAction` state when changing completion state
	PromptAction = FApp::IsUnattended() ? EAsyncTaskNotificationPromptAction::Unattended : EAsyncTaskNotificationPromptAction::None;
}

TSharedRef<SWidget> SSlateAsyncTaskNotificationWidget::AsWidget()
{
	return AsShared();
}

void SSlateAsyncTaskNotificationWidget::SetOwner(TSharedPtr<SNotificationItem> InOwningNotification)
{
	OwningNotification = InOwningNotification;
}

void SSlateAsyncTaskNotificationWidget::UpdateNotification(const FText& InTitleText, const FText& InProgressText, const FText& InPromptText, const FSimpleDelegate& InHyperlink, const FText& InHyperlinkText)
{
	TitleText = InTitleText;
	ProgressText = InProgressText;
	PromptText = InPromptText;
	Hyperlink = InHyperlink;
	HyperlinkText = InHyperlinkText;
}

void SSlateAsyncTaskNotificationWidget::SetPendingCompletionState(const EAsyncTaskNotificationState InPendingCompletionState, TSharedPtr<SSlateAsyncTaskNotificationWidget>* ExternalReferenceToReset)
{
	FScopeLock Lock(&CompletionCS);

	// Set the completion state and reset the external UI reference while we have the lock to avoid the game thread potentially destroying this notification (via a Tick) while another thread is still clearing its references to it
	PendingCompletionState = InPendingCompletionState;
	if (ExternalReferenceToReset)
	{
		ExternalReferenceToReset->Reset();
	}
}

void SSlateAsyncTaskNotificationWidget::SetCanCancel(const TAttribute<bool>& InCanCancel)
{
	FScopeLock Lock(&AttributesCS);

	bCanCancelAttr = InCanCancel;
}

void SSlateAsyncTaskNotificationWidget::SetKeepOpenOnSuccess(const TAttribute<bool>& InKeepOpenOnSuccess)
{
	FScopeLock Lock(&AttributesCS);

	bKeepOpenOnSuccessAttr = InKeepOpenOnSuccess;
}

void SSlateAsyncTaskNotificationWidget::SetKeepOpenOnFailure(const TAttribute<bool>& InKeepOpenOnFailure)
{
	FScopeLock Lock(&AttributesCS);

	bKeepOpenOnFailureAttr = InKeepOpenOnFailure;
}

EAsyncTaskNotificationPromptAction SSlateAsyncTaskNotificationWidget::GetPromptAction() const
{
	return PromptAction;
}

void SSlateAsyncTaskNotificationWidget::SyncAttributes()
{
	FScopeLock Lock(&AttributesCS);

	bCanCancel = bCanCancelAttr.Get(false);
	bKeepOpenOnSuccess = bKeepOpenOnSuccessAttr.Get(false);
	bKeepOpenOnFailure = bKeepOpenOnFailureAttr.Get(false);
}

EVisibility SSlateAsyncTaskNotificationWidget::GetTitleTextVisibility() const
{
	return (TitleText.IsEmpty())
		? EVisibility::Collapsed
		: EVisibility::Visible;
}

FText SSlateAsyncTaskNotificationWidget::GetTitleText() const
{
	return TitleText;
}

EVisibility SSlateAsyncTaskNotificationWidget::GetProgressTextVisibility() const
{
	return (ProgressText.IsEmpty())
		? EVisibility::Collapsed
		: EVisibility::Visible;
}

FText SSlateAsyncTaskNotificationWidget::GetProgressText() const
{
	return ProgressText;
}

EVisibility SSlateAsyncTaskNotificationWidget::GetThrobberVisibility() const
{
	return (NotificationState == EAsyncTaskNotificationState::Pending)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SSlateAsyncTaskNotificationWidget::GetStatusIconVisibility() const
{
	return (NotificationState == EAsyncTaskNotificationState::Success || NotificationState == EAsyncTaskNotificationState::Failure)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

const FSlateBrush* SSlateAsyncTaskNotificationWidget::GetStatusIconBrush() const
{
	return (NotificationState == EAsyncTaskNotificationState::Success)
		? FCoreStyle::Get().GetBrush("NotificationList.SuccessImage")
		: FCoreStyle::Get().GetBrush("NotificationList.FailImage");
}

bool SSlateAsyncTaskNotificationWidget::IsCancelButtonEnabled() const
{
	return bCanCancel && PromptAction == EAsyncTaskNotificationPromptAction::None;
}

EVisibility SSlateAsyncTaskNotificationWidget::GetCancelButtonVisibility() const
{
	return (bCanCancel && (NotificationState == EAsyncTaskNotificationState::Pending || NotificationState == EAsyncTaskNotificationState::Prompt))
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FReply SSlateAsyncTaskNotificationWidget::OnCancelButtonClicked()
{
	PromptAction = EAsyncTaskNotificationPromptAction::Cancel;
	return FReply::Handled();
}

bool SSlateAsyncTaskNotificationWidget::IsPromptButtonEnabled() const
{
	return PromptAction == EAsyncTaskNotificationPromptAction::None;
}

EVisibility SSlateAsyncTaskNotificationWidget::GetPromptButtonVisibility() const
{
	return (!FApp::IsUnattended() && NotificationState == EAsyncTaskNotificationState::Prompt)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FReply SSlateAsyncTaskNotificationWidget::OnPromptButtonClicked()
{
	PromptAction = EAsyncTaskNotificationPromptAction::Continue;
	return FReply::Handled();
}

FText SSlateAsyncTaskNotificationWidget::GetPromptButtonText() const
{
	return PromptText;
}

EVisibility SSlateAsyncTaskNotificationWidget::GetCloseButtonVisibility() const
{
	return (!FApp::IsUnattended() && ((bKeepOpenOnSuccess && NotificationState == EAsyncTaskNotificationState::Success) || (bKeepOpenOnFailure && NotificationState == EAsyncTaskNotificationState::Failure)))
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FReply SSlateAsyncTaskNotificationWidget::OnCloseButtonClicked()
{
	if (OwningNotification)
	{
		// Expire the notification immediately and ensure it fades quickly so that clicking the buttons feels responsive
		OwningNotification->SetExpireDuration(0.0f);
		OwningNotification->SetFadeOutDuration(0.5f);
		OwningNotification->ExpireAndFadeout();

		// Release our reference to our owner so that everything can be destroyed
		OwningNotification.Reset();
	}
	return FReply::Handled();
}

void SSlateAsyncTaskNotificationWidget::OnHyperlinkClicked() const
{
	Hyperlink.ExecuteIfBound();
}

FText SSlateAsyncTaskNotificationWidget::GetHyperlinkText() const
{
	return HyperlinkText;
}

EVisibility SSlateAsyncTaskNotificationWidget::GetHyperlinkVisibility() const
{
	return Hyperlink.IsBound() ? EVisibility::Visible : EVisibility::Collapsed;
}

SNotificationItem::ECompletionState SSlateAsyncTaskNotificationWidget::GetNotificationCompletionState() const
{
	if (OwningNotification)
	{
		return OwningNotification->GetCompletionState();
	}
	return SNotificationItem::CS_None;
}

void FSlateAsyncTaskNotificationImpl::Initialize(const FAsyncTaskNotificationConfig& InConfig)
{
	// Note: FCoreAsyncTaskNotificationImpl guarantees this is being called from the game thread
	if (!InConfig.bIsHeadless)
	{
		// Set-up the notification UI
		NotificationItemWidget = SNew(SSlateAsyncTaskNotificationWidget, InConfig);
		check(NotificationItemWidget);

		FNotificationInfo NotificationInfo(NotificationItemWidget);
		NotificationInfo.FadeInDuration = InConfig.FadeInDuration;
		NotificationInfo.FadeOutDuration = InConfig.FadeOutDuration;
		NotificationInfo.ExpireDuration = InConfig.ExpireDuration;
		NotificationInfo.bFireAndForget = false;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		check(NotificationItem);

		NotificationItemWidget->SetOwner(NotificationItem);
		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
	}

	// This calls UpdateNotification to update the UI initialized above
	FCoreAsyncTaskNotificationImpl::Initialize(InConfig);
}

void FSlateAsyncTaskNotificationImpl::SetCanCancel(const TAttribute<bool>& InCanCancel)
{
	if (NotificationItemWidget)
	{
		NotificationItemWidget->SetCanCancel(InCanCancel);
	}
}

void FSlateAsyncTaskNotificationImpl::SetKeepOpenOnSuccess(const TAttribute<bool>& InKeepOpenOnSuccess)
{
	if (NotificationItemWidget)
	{
		NotificationItemWidget->SetKeepOpenOnSuccess(InKeepOpenOnSuccess);
	}
}

void FSlateAsyncTaskNotificationImpl::SetKeepOpenOnFailure(const TAttribute<bool>& InKeepOpenOnSuccess)
{
	if (NotificationItemWidget)
	{
		NotificationItemWidget->SetKeepOpenOnFailure(InKeepOpenOnSuccess);
	}
}

void FSlateAsyncTaskNotificationImpl::UpdateNotification()
{
	FCoreAsyncTaskNotificationImpl::UpdateNotification();
	
	if (NotificationItemWidget)
	{
		// Update the notification text
		NotificationItemWidget->UpdateNotification(TitleText, ProgressText, PromptText, Hyperlink, HyperlinkText);

		// Complete the notification and remove our references to it in a single atomic operation if needed
		// NotificationItemWidget will be null once this call completes if completion is success or failure
		NotificationItemWidget->SetPendingCompletionState(State, State == EAsyncTaskNotificationState::Failure || State == EAsyncTaskNotificationState::Success ? &NotificationItemWidget : nullptr);
	}
}

EAsyncTaskNotificationPromptAction FSlateAsyncTaskNotificationImpl::GetPromptAction() const
{
	return NotificationItemWidget ? NotificationItemWidget->GetPromptAction() : EAsyncTaskNotificationPromptAction::Unattended;
}

#undef LOCTEXT_NAMESPACE
