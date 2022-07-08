// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateCore.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "UGSCore/ModalTask.h"

class SModalTaskWindow : public SWindow, private FRunnable
{
public:
	SLATE_BEGIN_ARGS(SModalTaskWindow) {}
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(FText, Message)
		SLATE_ARGUMENT(TSharedPtr<IModalTask>, Task)
	SLATE_END_ARGS()

	TSharedPtr<FModalTaskResult> Result;

	SModalTaskWindow();
	~SModalTaskWindow();

	void Construct(const FArguments& InArgs);
	EActiveTimerReturnType OnTickTimer(double CurrentTime, float DeltaTime);

private:
	FEvent* AbortEvent;
	FEvent* CloseEvent;
	FRunnableThread* Thread;
	TSharedPtr<IModalTask> Task;

	virtual uint32 Run() override;
};

TSharedRef<FModalTaskResult> ExecuteModalTask(TSharedPtr<SWidget> Parent, TSharedRef<IModalTask> Task, const FText& InTitle, const FText& InMessage);
