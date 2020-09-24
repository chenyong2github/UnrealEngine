// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminARFunctionLibrary.h"
#include "UnrealEngine.h"
#include "Engine/Engine.h"
#include "LatentActions.h"
#include "ARBlueprintLibrary.h"

#include "LuminARModule.h"
#include "LuminARTrackingSystem.h"
#include "LuminARTrackingSystem.h"


struct FLuminARStartSessionAction : public FPendingLatentAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;

	FLuminARStartSessionAction(const FLatentActionInfo& InLatentInfo)
		: FPendingLatentAction()
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
	{}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		TSharedPtr<FLuminARImplementation, ESPMode::ThreadSafe> LuminARSystem = FLuminARModule::GetLuminARSystem();
		bool bSessionStartedFinished = (LuminARSystem.IsValid()) ? LuminARSystem->GetStartSessionRequestFinished() : false;
		Response.FinishAndTriggerIf(bSessionStartedFinished, ExecutionFunction, OutputLink, CallbackTarget);
	}
#if WITH_EDITOR
	virtual FString GetDescription() const override
	{
		return FString::Printf(TEXT("Starting LuminAR tracking session"));
	}
#endif
};

void ULuminARSessionFunctionLibrary::StartLuminARSession(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, ULuminARSessionConfig* Configuration)
{
	UE_LOG(LogTemp, Log, TEXT("ULuminARSessionFunctionLibrary::StartLuminARSession"));
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		FLatentActionManager& LatentManager = World->GetLatentActionManager();
		if (LatentManager.FindExistingAction<FLuminARStartSessionAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
		{
			UARBlueprintLibrary::StartARSession(static_cast<UARSessionConfig*>(Configuration));
			FLuminARStartSessionAction* NewAction = new FLuminARStartSessionAction(LatentInfo);
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
	}
}

/************************************************************************/
/*  ULuminARFrameFunctionLibrary                                   */
/************************************************************************/
ELuminARTrackingState ULuminARFrameFunctionLibrary::GetTrackingState()
{
	TSharedPtr<FLuminARImplementation, ESPMode::ThreadSafe> LuminARSystem = FLuminARModule::GetLuminARSystem();
	return (LuminARSystem.IsValid()) ? LuminARSystem->GetTrackingState() : ELuminARTrackingState::StoppedTracking;
}

bool ULuminARFrameFunctionLibrary::LuminARLineTrace(UObject* WorldContextObject, const FVector2D& ScreenPosition, TSet<ELuminARLineTraceChannel> TraceChannels, TArray<FARTraceResult>& OutHitResults)
{
	ELuminARLineTraceChannel TraceChannelValue = ELuminARLineTraceChannel::None;
	for (ELuminARLineTraceChannel Channel : TraceChannels)
	{
		TraceChannelValue = TraceChannelValue | Channel;
	}

	TSharedPtr<FLuminARImplementation, ESPMode::ThreadSafe> LuminARSystem = FLuminARModule::GetLuminARSystem();
	if (LuminARSystem.IsValid())
	{
		LuminARSystem->ARLineTrace(ScreenPosition, TraceChannelValue, OutHitResults);
		return OutHitResults.Num() > 0;
	}

	return false;
}

ULuminARCandidateImage* ULuminARImageTrackingFunctionLibrary::AddLuminRuntimeCandidateImage(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth, bool bUseUnreliablePose, bool bImageIsStationary)
{
	// ForwardAxisAsNormal is default for anyone using this old function since that was the orientation we used to report before providing the option.
	return ULuminARImageTrackingFunctionLibrary::AddLuminRuntimeCandidateImageEx(SessionConfig, CandidateTexture, FriendlyName, PhysicalWidth, bUseUnreliablePose, bImageIsStationary, EMagicLeapImageTargetOrientation::ForwardAxisAsNormal);
}

ULuminARCandidateImage* ULuminARImageTrackingFunctionLibrary::AddLuminRuntimeCandidateImageEx(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth, bool bUseUnreliablePose, bool bImageIsStationary, EMagicLeapImageTargetOrientation InAxisOrientation)
{
	TSharedPtr<FLuminARImplementation, ESPMode::ThreadSafe> LuminARSystem = FLuminARModule::GetLuminARSystem();
	if (LuminARSystem.IsValid())
	{
		return LuminARSystem->AddLuminRuntimeCandidateImage(SessionConfig, CandidateTexture, FriendlyName, PhysicalWidth, bUseUnreliablePose, bImageIsStationary, InAxisOrientation);
	}
	else
	{
		return nullptr;
	}
}
