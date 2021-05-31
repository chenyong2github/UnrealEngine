// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportConfigurationHelpers_OpenColorIO.h"

#include "Render/Viewport/DisplayClusterViewport.h"

#include "DisplayClusterSceneViewExtensions.h"
#include "DisplayClusterRootActor.h"

#include "OpenColorIODisplayExtension.h"

void FDisplayClusterViewportConfigurationHelpers_OpenColorIO::Update(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, const FOpenColorIODisplayConfiguration& InOCIO_Configuration)
{
	const FOpenColorIODisplayConfiguration* OCIO_Configuration = nullptr;
	if (InOCIO_Configuration.bIsEnabled && InOCIO_Configuration.ColorConfiguration.IsValid())
	{
		OCIO_Configuration = &InOCIO_Configuration;
	}
	else
	{
		OCIO_Configuration = RootActor.GetViewportOCIO(DstViewport.GetId());
	}

	if (OCIO_Configuration && OCIO_Configuration->bIsEnabled && OCIO_Configuration->ColorConfiguration.IsValid())
	{
		// Create/Update OCIO:
		if (DstViewport.OpenColorIODisplayExtension.IsValid() == false)
		{
			DstViewport.OpenColorIODisplayExtension = FSceneViewExtensions::NewExtension<FOpenColorIODisplayExtension>(nullptr);
			if (DstViewport.OpenColorIODisplayExtension.IsValid())
			{
				// assign active func
				DstViewport.OpenColorIODisplayExtension->IsActiveThisFrameFunctions.Reset(1);
				DstViewport.OpenColorIODisplayExtension->IsActiveThisFrameFunctions.Add(DstViewport.GetSceneViewExtensionIsActiveFunctor());
			}
		}

		if (DstViewport.OpenColorIODisplayExtension.IsValid())
		{
			// Update configuration
			DstViewport.OpenColorIODisplayExtension->SetDisplayConfiguration(*OCIO_Configuration);
		}
	}
	else
	{
		// Remove OICO ref
		DstViewport.OpenColorIODisplayExtension.Reset();
	}
}

