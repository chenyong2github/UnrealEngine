// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "OpenColorIOColorSpace.h"
#include "OpenColorIORendering.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "Templates/SharedPointer.h"

/**
 * nDisplay OCIO implementation.
 * 
 */
class FDisplayClusterViewport_OpenColorIO
	: public TSharedFromThis<FDisplayClusterViewport_OpenColorIO, ESPMode::ThreadSafe>
{
public:
	FDisplayClusterViewport_OpenColorIO(const FOpenColorIOColorConversionSettings& InDisplayConfiguration);
	virtual ~FDisplayClusterViewport_OpenColorIO();

public:
	/** Update render thread resources. */
	void UpdateOpenColorIORenderPassResources();

	/** Setup view for OCIO.
	 *
	 * @param InOutViewFamily - [In,Out] ViewFamily.
	 * @param InOutView       - [In,Out] View.
	 *
	 * @return - none.
	 */
	void SetupSceneView(class FSceneViewFamily& InOutViewFamily, class FSceneView& InOutView) const;

	/** Add OCIO render pass.
	 *
	 * @param GraphBuilder      - RDG interface.
	 * @param InViewportContext - DC viewport context.
	 * @param InputTextureRHI   - Source texture.
	 * @param InputRect         - Source rec.
	 * @param OutputTextureRHI  - Destination texture.
	 * @param OutputRect        - Destination rec.
	 *
	 * @return - true if success.
	 */
	bool AddPass_RenderThread(class FRDGBuilder& GraphBuilder, const class FDisplayClusterViewport_Context& InViewportContext,
		FRHITexture2D* InputTextureRHI, const FIntRect& InputRect, FRHITexture2D* OutputTextureRHI, const FIntRect& OutputRect) const;

	/** Is OCIO enabled for render thread */
	bool IsEnabled_RenderThread() const;

	/** Compare two OCIO configurations.
	 *
	 * @param InDisplayConfiguration - configuration to compare with.
	 *
	 * @return - true if equal.
	 */
	bool IsDisplayConfigurationEquals(const FOpenColorIOColorConversionSettings& InDisplayConfiguration) const;

	/** Get current OCIO configuration. */
	const FOpenColorIOColorConversionSettings& GetDisplayConfiguration() const
	{
		return DisplayConfiguration;
	}

private:
	/** Cached pass resources required to apply conversion for render thread. */
	FOpenColorIORenderPassResources CachedResourcesRenderThread;

	/** Configuration to apply during post render callback. */
	FOpenColorIOColorConversionSettings DisplayConfiguration;

	/** Shader resources state. */
	bool bShaderResourceValid = true;

	/** Configuration data state. */
	bool bConfigurationDataValid = true;
};
