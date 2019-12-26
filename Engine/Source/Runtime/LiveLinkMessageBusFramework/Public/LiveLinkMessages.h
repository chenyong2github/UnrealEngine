// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/Object.h"
#include "LiveLinkRefSkeleton.h"
#include "LiveLinkTypes.h"
#include "LiveLinkMessages.generated.h"

struct LIVELINKMESSAGEBUSFRAMEWORK_API FLiveLinkMessageAnnotation
{
	static FName SubjectAnnotation;
	static FName RoleAnnotation;
};

USTRUCT()
struct FLiveLinkPingMessage
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid PollRequest;

	UPROPERTY()
	int32 LiveLinkVersion = 1;

	// default constructor for the receiver
	FLiveLinkPingMessage() = default;

	FLiveLinkPingMessage(const FGuid& InPollRequest, int32 InLiveLinkVersion) : PollRequest(InPollRequest), LiveLinkVersion(InLiveLinkVersion) {}
};

USTRUCT()
struct FLiveLinkPongMessage
{
	GENERATED_BODY()

	UPROPERTY()
	FString ProviderName;

	UPROPERTY()
	FString MachineName;

	UPROPERTY()
	FGuid PollRequest;

	UPROPERTY()
	int32 LiveLinkVersion = 1;

	UPROPERTY()
	double CreationPlatformTime = -1.0;

	// default constructor for the receiver
	FLiveLinkPongMessage() = default;

	FLiveLinkPongMessage(const FString& InProviderName, const FString& InMachineName, const FGuid& InPollRequest) : ProviderName(InProviderName), MachineName(InMachineName), PollRequest(InPollRequest), CreationPlatformTime(FPlatformTime::Seconds()) {}
};

USTRUCT()
struct FLiveLinkConnectMessage
{
	GENERATED_BODY()

	UPROPERTY()
	int32 LiveLinkVersion = 1;
};

USTRUCT()
struct FLiveLinkHeartbeatMessage
{
	GENERATED_BODY()
};

USTRUCT()
struct FLiveLinkClearSubject
{
	GENERATED_BODY()

	// Name of the subject to clear
	UPROPERTY()
	FName SubjectName;

	FLiveLinkClearSubject() {}
	FLiveLinkClearSubject(const FName& InSubjectName) : SubjectName(InSubjectName) {}
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS
USTRUCT()
struct UE_DEPRECATED(4.23, "FLiveLinkSubjectDataMessage is deprecated. Please use the LiveLink animation role.") FLiveLinkSubjectDataMessage
{
	GENERATED_BODY()

	UPROPERTY()
	FLiveLinkRefSkeleton RefSkeleton;

	UPROPERTY()
	FName SubjectName;
};

USTRUCT()
struct UE_DEPRECATED(4.23, "FLiveLinkSubjectDataMessage is deprecated. Please use the LiveLink animation role.") FLiveLinkSubjectFrameMessage
{
	GENERATED_BODY()

	UPROPERTY()
	FName SubjectName;

	// Bone Transform data for this frame
	UPROPERTY()
	TArray<FTransform> Transforms;

	// Curve data for this frame
	UPROPERTY()
	TArray<FLiveLinkCurveElement> Curves;

	// Subject MetaData for this frame
	UPROPERTY()
	FLiveLinkMetaData MetaData;

	// Incrementing time for interpolation
	UPROPERTY()
	double Time;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS