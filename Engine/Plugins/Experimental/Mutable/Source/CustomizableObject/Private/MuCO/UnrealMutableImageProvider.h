// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "HAL/Platform.h"
#include "MuR/Image.h"
#include "MuR/System.h"
#include "MuR/Types.h"
#include "UObject/WeakObjectPtrTemplates.h"


/** Implementation of a mutable core provider for image parameters that are application-specific. */
class FUnrealMutableImageProvider : public mu::ImageParameterGenerator
{
public:

	// mu::ImageParameterGenerator interface
	// Thread: Mutable
	mu::ImagePtr GetImage(mu::EXTERNAL_IMAGE_ID id) override;

	// Own interface
	// Thread: Game
	void CacheImage(mu::EXTERNAL_IMAGE_ID id);
	void ClearCache();

	/** List of actual image providers that have been registered to the CustomizableObjectSystem. */
	TArray< TWeakObjectPtr<class UCustomizableSystemImageProvider> > ImageProviders;

private:

	/** This will be called if an image Id has been requested by Mutable core but it has not been provided by any provider. */
	mu::ImagePtr CreateDummy();

	/** List of external textures that may be required for the current instance under construction.
	* This is only written from the game thread before scheduling a new instance update, and it
	* is read from the mutable thread during the update.
	*/
	TMap<uint64, mu::ImagePtr> ExternalImagesForCurrentInstance;

};


