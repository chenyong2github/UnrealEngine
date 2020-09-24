// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"

#include "Modules/ModuleManager.h"
#include "IMediaInfoModule.h"
#include "IMediaModule.h"
#include "Misc/Guid.h"

#define LOCTEXT_NAMESPACE "MediaInfoModule"

// ----------------------------------------------------------------------------------------------

class FMediaInfoModule : public IMediaInfoModule
{
public:
	void StartupModule() override
	{
	}

	void Initialize(IMediaModule* MediaModule) override
	{
		MediaModule->RegisterPlatform(TEXT("Windows"), FGuid(0xd1d5f296, 0xff834a87, 0xb20faaa9, 0xd6b8e9a6), this);
		MediaModule->RegisterPlatform(TEXT("Android"), FGuid(0x3619ea87, 0xde704a48, 0xbb155175, 0x4423c26a), this);
		MediaModule->RegisterPlatform(TEXT("IOS"), FGuid(0x988eba73, 0xf971495b, 0xafb09639, 0xf8c796bd), this);
		MediaModule->RegisterPlatform(TEXT("TVOS"), FGuid(0xa478294f, 0xbd0d4ec0, 0x8830b6d4, 0xd219c1a4), this);
		MediaModule->RegisterPlatform(TEXT("Mac"), FGuid(0x003be296, 0x17004f0c, 0x8e1f7860, 0x81efbb1f), this);
		MediaModule->RegisterPlatform(TEXT("Linux"), FGuid(0x115de4fe, 0x241b465b, 0x970a872f, 0x3167492a), this);
		MediaModule->RegisterPlatform(TEXT("Unix"), FGuid(0xa15b98db, 0x84ca4b5a, 0x84ababff, 0xb9d552f3), this);
		MediaModule->RegisterPlatform(TEXT("Hololens"), FGuid(0x0df604e1, 0x12e44452, 0x80bee1c7, 0x4eb934b1), this);
		MediaModule->RegisterPlatform(TEXT("Lumin"), FGuid(0xa20fce56, 0xea274aad, 0xb4ea8567, 0xaa6bbab3), this);
	}
};

IMPLEMENT_MODULE(FMediaInfoModule, MediaInfo);

#undef LOCTEXT_NAMESPACE
