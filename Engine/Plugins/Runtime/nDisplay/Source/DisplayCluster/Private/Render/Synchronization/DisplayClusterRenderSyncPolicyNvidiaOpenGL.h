// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaBase.h"

class  FOpenGLViewport;
struct FPlatformOpenGLContext;

/**
 * OpenGL NVIDIA SwapLock synchronization policy
 */
class FDisplayClusterRenderSyncPolicyNvidiaOpenGL
	: public FDisplayClusterRenderSyncPolicyNvidiaBase
{
public:
	FDisplayClusterRenderSyncPolicyNvidiaOpenGL();
	virtual ~FDisplayClusterRenderSyncPolicyNvidiaOpenGL();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRenderSyncPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool SynchronizeClusterRendering(int32& InOutSyncInterval) override;

private:
	// Joins swap groups and binds to a swap barrier
	bool InitializeNvidiaSwapLock();
	void InitializeOpenGLCapabilities();
	void UpdateSwapInterval(int32 InSyncInterval) const;

private:
	FOpenGLViewport* OpenGLViewport = nullptr;
	FPlatformOpenGLContext* OpenGLContext = nullptr;
};
