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

#if 0
protected:
	// Joins swap groups and binds to a swap barrier
	bool InitializeNvidiaSwapLock();
	void UpdateSwapInterval(int32 InSyncInterval) const;

private:
	FOpenGLViewport* OpenGLViewport = nullptr;
	FPlatformOpenGLContext* OpenGLContext = nullptr;
#endif
};
