// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDisplayClusterStringSerializable.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// Base interface for config data holders
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextBase : public IDisplayClusterStringSerializable
{
	virtual ~FDisplayClusterConfigurationTextBase()
	{ }

	// Prints in human readable format
	virtual FString ToString() const
	{ return FString("[]"); }

	// Currently no need to serialize the data
	virtual FString SerializeToString() const override
	{ return FString(); }

	// Deserialization from config file
	virtual bool    DeserializeFromString(const FString& Line) override
	{ return true; }
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Config info
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextInfo : public FDisplayClusterConfigurationTextBase
{
	FString Version;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Cluster node configuration (separate application)
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextClusterNode : public FDisplayClusterConfigurationTextBase
{
	FString Id;
	FString Addr;
	FString WindowId;
	bool    IsMaster = false;
	int32   Port_CS  = 41001;
	int32   Port_SS  = 41002;
	int32   Port_CE  = 41003;
	int32   Port_CEB = 41004;
	bool    SoundEnabled = false;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Application window configuration
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextWindow : public FDisplayClusterConfigurationTextBase
{
	FString Id;
	TArray<FString> ViewportIds;
	TArray<FString> PostprocessIds;
	bool IsFullscreen = false;
	int32 WinX = 0;
	int32 WinY = 0;
	int32 ResX = 0;
	int32 ResY = 0;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Viewport configuration
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextViewport : public FDisplayClusterConfigurationTextBase
{
	FString   Id;
	FString   ProjectionId;
	FString   CameraId;
	FIntPoint Loc  = FIntPoint::ZeroValue;
	FIntPoint Size = FIntPoint::ZeroValue;
	float     BufferRatio = 1.f;
	int       GPUIndex = -1;                 // Force custom mgpu index for this viewport {view->bOverrideGPUMask=true; view->GPUMask=GPUIndex; }
	bool      IsShared = false;              // Share this viewport for all (scene context textures, backbuffer)

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Postprocess configuration
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextPostprocess : public FDisplayClusterConfigurationTextBase
{
	FString Id;
	FString Type;
	FString ConfigLine;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Scene node configuration (DisplayCluster hierarchy is built from such nodes)
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextSceneNode : public FDisplayClusterConfigurationTextBase
{
	FString  Id;
	FString  ParentId;
	FVector  Loc = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Projection screen configuration (used for asymmetric frustum calculation)
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextScreen : public FDisplayClusterConfigurationTextSceneNode
{
	FVector2D Size = FVector2D::ZeroVector;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Camera configuration (DisplayCluster camera)
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextCamera : public FDisplayClusterConfigurationTextSceneNode
{
	float EyeDist = 0.064f;
	bool  EyeSwap = false;
	int   ForceOffset = 0;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// General DisplayCluster configuration
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextGeneral : public FDisplayClusterConfigurationTextBase
{
	int32 SwapSyncPolicy = 1;
	int32 NativeInputSyncPolicy = 1;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// NVIDIA configuration
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextNvidia : public FDisplayClusterConfigurationTextBase
{
	int32 SyncGroup   = 1;
	int32 SyncBarrier = 1;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Network configuration
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextNetwork : public FDisplayClusterConfigurationTextBase
{
	int32 ClientConnectTriesAmount    = 10;    // times
	int32 ClientConnectRetryDelay     = 1000;  // ms
	int32 BarrierGameStartWaitTimeout = 30000; // ms
	int32 BarrierWaitTimeout          = 5000;  // ms

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Debug settings
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextDebug : public FDisplayClusterConfigurationTextBase
{
	bool  DrawStats = false;
	bool  LagSimulateEnabled = false;
	float LagMaxTime = 0.5f; // seconds

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Custom development settings
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextCustom : public FDisplayClusterConfigurationTextBase
{
	TMap<FString, FString> Params;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Projection data
//////////////////////////////////////////////////////////////////////////////////////////////
struct FDisplayClusterConfigurationTextProjection : public FDisplayClusterConfigurationTextBase
{
	FString Id;
	FString Type;
	FString Params;

	virtual FString ToString() const override;
	virtual bool    DeserializeFromString(const FString& Line) override;
};
