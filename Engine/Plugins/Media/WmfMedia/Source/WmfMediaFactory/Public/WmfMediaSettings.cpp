// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "WmfMediaSettings.h"

#if WITH_EDITOR

void UWmfMediaSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& InPropertyChangedEvent)
{
	if (InPropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UWmfMediaSettings, EnableHAPCodec))
	{
		if (EnableHAPCodec)
		{
			AllowNonStandardCodecs = true;
			HardwareAcceleratedVideoDecoding = true;
			SaveConfig(CPF_Config, *GetDefaultConfigFilename());
		}
	}

	Super::PostEditChangeChainProperty(InPropertyChangedEvent);
}

#endif //WITH_EDITOR

