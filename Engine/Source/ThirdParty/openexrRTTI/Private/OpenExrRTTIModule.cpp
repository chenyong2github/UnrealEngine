// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOpenExrRTTIModule.h"
#include "Modules/ModuleManager.h"

THIRD_PARTY_INCLUDES_START
#include "OpenEXR/include/ImfStringAttribute.h"
#include "OpenEXR/include/ImfIntAttribute.h"
#include "OpenEXR/include/ImfFloatAttribute.h"
THIRD_PARTY_INCLUDES_END


class FOpenExrRTTIModule : public IOpenExrRTTIModule
{
public:
	virtual void AddFileMetadata(const TMap<FString, FStringFormatArg>& InMetadata, Imf::Header& InHeader) override
	{
		for (const TPair<FString, FStringFormatArg>& KVP : InMetadata)
		{
			OPENEXR_IMF_INTERNAL_NAMESPACE::Attribute* Attribute = nullptr;
			switch (KVP.Value.Type)
			{
			case FStringFormatArg::EType::Int:
				Attribute = new OPENEXR_IMF_INTERNAL_NAMESPACE::IntAttribute(KVP.Value.IntValue);
				break;
			case FStringFormatArg::EType::Double:
				// Not all EXR readers support the double attribute, and we can't tell double from float in the FStringFormatArgs,
				// so unfortunately we have to downgrade them here. 
				Attribute = new OPENEXR_IMF_INTERNAL_NAMESPACE::FloatAttribute(KVP.Value.DoubleValue);
				break;
			case FStringFormatArg::EType::String:
				Attribute = new OPENEXR_IMF_INTERNAL_NAMESPACE::StringAttribute(std::string(TCHAR_TO_ANSI(*KVP.Value.StringValue)));
				break;
			case FStringFormatArg::EType::StringLiteral:
				Attribute = new OPENEXR_IMF_INTERNAL_NAMESPACE::StringAttribute(std::string(TCHAR_TO_ANSI(KVP.Value.StringLiteralValue)));
				break;

			case FStringFormatArg::EType::UInt:
			default:
				ensureMsgf(false, TEXT("Failed to add Metadata to EXR file, unsupported type."));
			}

			if (Attribute)
			{
				InHeader.insert(std::string(TCHAR_TO_ANSI(*KVP.Key)), *Attribute);
				delete Attribute;
			}
		}
	}
};

IMPLEMENT_MODULE(FOpenExrRTTIModule, UEOpenExrRTTI);
