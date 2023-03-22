// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DisplayClusterConfigurationTypes_Base.h"

#include "MediaPlayer.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "MediaOutput.h"

#include "DisplayClusterConfigurationTypes_Media.generated.h"


/*
 * Base media output synchronization policy class
 */
UCLASS(Abstract, editinlinenew, BlueprintType, hidecategories = (Object))
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterMediaOutputSynchronizationPolicy
	: public UObject
{
	GENERATED_BODY()

public:
	/** Returns true if specified media capture type can be synchonized by the policy implementation */
	virtual bool IsCaptureTypeSupported(UMediaCapture* MediaCapture) const
	{
		return true;
	}

	/** Starts synchronization of specific output stream (capture device). Returns false if failed. */
	virtual bool StartSynchronization(UMediaCapture* MediaCapture, const FString& MediaId) PURE_VIRTUAL(UDisplayClusterMediaOutputSynchronizationPolicy::StartSynchronization, return false;);

	/** Stops synchronization of specific output stream (capture device). */
	virtual void StopSynchronization() PURE_VIRTUAL(UDisplayClusterMediaOutputSynchronizationPolicy::StopSynchronization, );

	/** Returns true if currently synchronising a media output. */
	virtual bool IsRunning() PURE_VIRTUAL(UDisplayClusterMediaOutputSynchronizationPolicy::IsRunning, return false; );
};


PRAGMA_DISABLE_DEPRECATION_WARNINGS

/*
 * Media settings for viewports and backbuffer
 */
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationMedia
{
	GENERATED_BODY()

public:
	/** Enable/disable media */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media")
	bool bEnable = false;

	/** Media source to use */
	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite, Category = "Media")
	TObjectPtr<UMediaSource> MediaSource;

	/** Media output to use */
	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite, Category = "Output")
	TObjectPtr<UMediaOutput> MediaOutput;

	/** Media output synchronization policy */
	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (DisplayName = "Capture Synchronization"))
	TObjectPtr<UDisplayClusterMediaOutputSynchronizationPolicy> OutputSyncPolicy;

protected:
#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.2, "This property has been deprecated")
	UPROPERTY()
	FString MediaSharingNode_DEPRECATED;
#endif // WITH_EDITORONLY_DATA
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS


/*
 * Input media group
 */
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationMediaInputGroup
{
	GENERATED_BODY()

public:
	/** Cluster nodes that use media source below */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Media, meta = (ClusterItemType = ClusterNodes))
	FDisplayClusterConfigurationClusterItemReferenceList ClusterNodes;

	/** Media source to use */
	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite, Category = "Media")
	TObjectPtr<UMediaSource> MediaSource;
};


/*
 * Output media group
 */
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationMediaOutputGroup
{
	GENERATED_BODY()

public:
	/** Cluster nodes that export media via MediaOutput below */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Media, meta = (ClusterItemType = ClusterNodes))
	FDisplayClusterConfigurationClusterItemReferenceList ClusterNodes;

	/** Media output to use */
	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite, Category = "Media")
	TObjectPtr<UMediaOutput> MediaOutput;

	/** Media output synchronization policy */
	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite, Category = "Media", meta = (DisplayName = "Capture Synchronization"))
	TObjectPtr<UDisplayClusterMediaOutputSynchronizationPolicy> OutputSyncPolicy;
};


/*
 * Media settings for ICVFX cameras
 */
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationMediaICVFX
{
	GENERATED_BODY()

public:
	/** Enable/disable media */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media")
	bool bEnable = false;

	/** Media output mapping */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media")
	TArray<FDisplayClusterConfigurationMediaOutputGroup> MediaOutputGroups;

	/** Media input mapping */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media")
	TArray<FDisplayClusterConfigurationMediaInputGroup> MediaInputGroups;

public:
	/** Returns true if a specific cluster node has media source assigned */
	bool IsMediaInputAssigned(const FString& NodeId) const;

	/** Returns true if a specific cluster node has media source assigned */
	bool IsMediaOutputAssigned(const FString& NodeId) const;

	/** Returns media source bound to a specific cluster node */
	UMediaSource* GetMediaSource(const FString& NodeId) const;

	/** Returns media output bound to a specific cluster node */
	UMediaOutput* GetMediaOutput(const FString& NodeId) const;

	/** Returns media output sync policy bound to a specific cluster node */
	UDisplayClusterMediaOutputSynchronizationPolicy* GetOutputSyncPolicy(const FString& NodeId) const;

	UE_DEPRECATED(5.2, "This function has beend deprecated.")
	bool IsMediaSharingNode(const FString& InNodeId) const
	{
		return false;
	}
};


/*
 * Global media settings
 */
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationGlobalMediaSettings
{
	GENERATED_BODY()

public:
	/** Media latency */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media", meta = (ClampMin = "0", ClampMax = "9", UIMin = "0", UIMax = "9"))
	int32 Latency = 0;
};
