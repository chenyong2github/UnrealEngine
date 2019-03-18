// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayMediaEncoderCommon.h"

// AMF includes Windows headers which triggers redefinition warnings
// include UE4 Windows definition before that
#include "Windows/WindowsHWrapper.h"

THIRD_PARTY_INCLUDES_START
	#include "AmdAmf/core/Result.h"
	#include "AmdAmf/core/Factory.h"
	#include "AmdAmf/components/VideoEncoderVCE.h"
	#include "AmdAmf/core/Compute.h"
	#include "AmdAmf/core/Plane.h"
THIRD_PARTY_INCLUDES_END

DECLARE_LOG_CATEGORY_EXTERN(AmdAmf, Log, VeryVerbose);

#define CHECK_AMF_RET(AMF_call)\
{\
	AMF_RESULT Res = AMF_call;\
	if (Res != AMF_OK)\
	{\
		UE_LOG(AmdAmf, Error, TEXT("`" #AMF_call "` failed with error code: %d"), Res);\
		/*check(false);*/\
		return false;\
	}\
}

// enumerates all available properties of AMFPropertyStorage interface and logs their name,
// current and default values and other info
inline bool LogAmfPropertyStorage(amf::AMFPropertyStorageEx* PropertyStorage)
{
	SIZE_T NumProps = PropertyStorage->GetPropertiesInfoCount();
	for (int i = 0; i != NumProps; ++i)
	{
		const amf::AMFPropertyInfo* Info;
		CHECK_AMF_RET(PropertyStorage->GetPropertyInfo(i, &Info));

		if (Info->accessType != amf::AMF_PROPERTY_ACCESS_PRIVATE)
		{
			amf::AMFVariant Value;
			CHECK_AMF_RET(PropertyStorage->GetProperty(Info->name, &Value));

			FString EnumDesc;
			if (Info->pEnumDescription)
			{
				int j = 0;
				for (; /*j != Value.ToInt32()*/; ++j)
				{
					if (Info->pEnumDescription[j].value == Value.ToInt32())
					{
						break;
					}
				}
				EnumDesc = TEXT(" ") + FString(Info->pEnumDescription[j].name);
			}

			UE_LOG(AmdAmf, Log, TEXT("Prop %s (%s): value: %s%s, default value: %s (%s - %s), access: %d"),
				Info->name,
				Info->desc,
				Value.ToWString().c_str(),
				*EnumDesc,
				amf::AMFVariant{ Info->defaultValue }.ToWString().c_str(),
				amf::AMFVariant{ Info->minValue }.ToWString().c_str(),
				amf::AMFVariant{ Info->maxValue }.ToWString().c_str(),
				static_cast<int>(Info->accessType));
		}
		else
		{
			UE_LOG(AmdAmf, VeryVerbose, TEXT("Prop: %s (%s) - PRIVATE"), Info->name, Info->desc);
		}
	}

	return true;
}

