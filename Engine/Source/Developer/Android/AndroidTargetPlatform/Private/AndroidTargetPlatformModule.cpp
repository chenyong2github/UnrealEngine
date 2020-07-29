// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Android/AndroidPlatformProperties.h"
#include "Interfaces/ITargetPlatformModule.h"
#include "Common/TargetPlatformBase.h"
#include "Interfaces/IAndroidDeviceDetection.h"
#include "Interfaces/IAndroidDeviceDetectionModule.h"
#include "AndroidTargetDevice.h"
#include "AndroidTargetPlatform.h"
#include "IAndroidTargetPlatformModule.h"

#define LOCTEXT_NAMESPACE "FAndroidTargetPlatformModule"

/**
 * Module for the Android target platform.
 */
class FAndroidTargetPlatformModule : public IAndroidTargetPlatformModule
{
public:

	virtual void GetTargetPlatforms(TArray<ITargetPlatform*>& TargetPlatforms) override
	{
		if (FAndroidTargetPlatform::IsUsable())
		{
			for (int32 Type = 0; Type < 2; Type++)
			{
				bool bIsClient = Type == 1;
				// flavorless must come first
				SinglePlatforms.Add(new FAndroidTargetPlatform(bIsClient, nullptr));
				SinglePlatforms.Add(new FAndroid_ASTCTargetPlatform(bIsClient));
				SinglePlatforms.Add(new FAndroid_DXTTargetPlatform(bIsClient));
				SinglePlatforms.Add(new FAndroid_ETC2TargetPlatform(bIsClient));

				// thse are used in NotifyMultiSelectedFormatsChanged, so track in another array
				MultiPlatforms.Add(new FAndroid_MultiTargetPlatform(bIsClient));
			}

			// join the single and the multi into one
			TargetPlatforms.Append(SinglePlatforms);
			TargetPlatforms.Append(MultiPlatforms);

			// set up the multi platforms now that we have all the other platforms ready to go
			NotifyMultiSelectedFormatsChanged();
		}
	}


	virtual void NotifyMultiSelectedFormatsChanged() override
	{
		for (FAndroid_MultiTargetPlatform* TP : MultiPlatforms)
		{
			TP->LoadFormats(SinglePlatforms);
		}
		// @todo multi needs to be passed this event!
	}


private:

	/** Holds the specific types of target platforms for NotifyMultiSelectedFormatsChanged */
	TArray<FAndroidTargetPlatform*> SinglePlatforms;
	TArray<FAndroid_MultiTargetPlatform*> MultiPlatforms;
};


#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE( FAndroidTargetPlatformModule, AndroidTargetPlatform);
