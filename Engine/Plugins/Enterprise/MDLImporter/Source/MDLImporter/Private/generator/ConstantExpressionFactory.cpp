// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef USE_MDLSDK

#include "ConstantExpressionFactory.h"

#include "generator/MaterialExpressions.h"
#include "generator/MaterialTextureFactory.h"
#include "mdl/MdlSdkDefines.h"
#include "mdl/Utility.h"

#include "Engine/Texture2D.h"

MDLSDK_INCLUDES_START
#include "mi/neuraylib/itexture.h"
#include "mi/neuraylib/itransaction.h"
#include "mi/neuraylib/ivalue.h"
#include "mi/neuraylib/ivector.h"
MDLSDK_INCLUDES_END

namespace Generator
{
	FConstantExpressionFactory::FConstantExpressionFactory()
	    : TextureFactory(nullptr)
	{
	}

	FMaterialExpressionConnectionList FConstantExpressionFactory::CreateExpression(mi::neuraylib::ITransaction& Transaction,
	                                                                                   const mi::neuraylib::IValue& MDLConstant)
	{
		check(CurrentMDLMaterial);
		check(CurrentMaterial);
		check(TextureFactory);

		const mi::neuraylib::IValue::Kind Kind = MDLConstant.get_kind();
		switch (Kind)
		{
			case mi::neuraylib::IValue::VK_BOOL:
			{
				auto Value = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_bool>());
				return {NewMaterialExpressionStaticBool(CurrentMaterial, Value->get_value())};
			}
			case mi::neuraylib::IValue::VK_INT:
			{
				auto Value = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_int>());
				return {NewMaterialExpressionConstant(CurrentMaterial, (float)Value->get_value())};
			}
			case mi::neuraylib::IValue::VK_ENUM:
			{
				auto Value = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_enum>());
				return {NewMaterialExpressionConstant(CurrentMaterial, (float)Value->get_value())};
			}
			case mi::neuraylib::IValue::VK_FLOAT:
			{
				auto Value = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_float>());
				return {NewMaterialExpressionConstant(CurrentMaterial, Value->get_value())};
			}
			case mi::neuraylib::IValue::VK_DOUBLE:
			{
				auto Value = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_double>());
				return {NewMaterialExpressionConstant(CurrentMaterial, (float)Value->get_value())};
			}
			case mi::neuraylib::IValue::VK_STRING:
			{
				return {};
			}
			case mi::neuraylib::IValue::VK_VECTOR:
			{
				auto Value = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_vector>());

				mi::base::Handle<const mi::neuraylib::IValue_float> Values[4];
				for (mi::Size i = 0; i < Value->get_size(); i++)
				{
					auto Handle = mi::base::make_handle(Value->get_value(i));
					Values[i]   = mi::base::make_handle(Handle->get_interface<const mi::neuraylib::IValue_float>());
				}
				switch (Value->get_size())
				{
					case 2:
						return {NewMaterialExpressionConstant(CurrentMaterial, Values[0]->get_value(), Values[1]->get_value())};
					case 3:
						return {NewMaterialExpressionConstant(  //
						    CurrentMaterial, Values[0]->get_value(), Values[1]->get_value(), Values[2]->get_value())};
					case 4:
						return {NewMaterialExpressionConstant(  //
						    CurrentMaterial, Values[0]->get_value(), Values[1]->get_value(), Values[2]->get_value(), Values[3]->get_value())};
				}
				break;
			}
			case mi::neuraylib::IValue::VK_MATRIX:
			{
				auto Value = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_matrix>());

				FMaterialExpressionConnectionList Result;
				Result.Reserve(Value->get_size());
				for (mi::Size i = 0; i < Value->get_size(); i++)
				{
					auto Handle = mi::base::make_handle(Value->get_value(i));
					Result.Append(CreateExpression(Transaction, *Handle));
				}

				return Result;
			}
			case mi::neuraylib::IValue::VK_COLOR:
			{
				auto Value = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_color>());
				auto Red   = mi::base::make_handle<const mi::neuraylib::IValue_float>(Value->get_value(0));
				auto Green = mi::base::make_handle<const mi::neuraylib::IValue_float>(Value->get_value(1));
				auto Blue  = mi::base::make_handle<const mi::neuraylib::IValue_float>(Value->get_value(2));
				return {NewMaterialExpressionConstant(CurrentMaterial, Red->get_value(), Green->get_value(), Blue->get_value())};
			}
			case mi::neuraylib::IValue::VK_ARRAY:
			{
				auto Value = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_array>());

				FMaterialExpressionConnectionList Result;
				for (mi::Size i = 0; i < Value->get_size(); i++)
				{
					auto Handle = mi::base::make_handle(Value->get_value(i));
					Result.Append(CreateExpression(Transaction, *Handle));
				}

				return Result;
			}
			case mi::neuraylib::IValue::VK_STRUCT:
			{
				auto Value = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_struct>());

				FMaterialExpressionConnectionList Result;
				Result.Reserve(Value->get_size());
				for (mi::Size i = 0; i < Value->get_size(); i++)
				{
					auto Handle = mi::base::make_handle(Value->get_value(i));
					Result.Append(CreateExpression(Transaction, *Handle));
				}

				return Result;
			}
			case mi::neuraylib::IValue::VK_INVALID_DF:
			{
				return {NewMaterialExpressionMakeMaterialAttributes(CurrentMaterial, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f)};
			}
			case mi::neuraylib::IValue::VK_TEXTURE:
			{
				auto Handle = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_texture>());
				const mi::base::Handle<const mi::neuraylib::ITexture> MDLTexture(Transaction.access<mi::neuraylib::ITexture>(Handle->get_value()));

				if (MDLTexture)
				{
					Common::FTextureProperty Property;
					Property.Path = Mdl::Util::GetTextureFileName(MDLTexture.get());
					float Gamma = MDLTexture->get_effective_gamma();
					Property.bIsSRGB = Gamma != 1.0;
					if (ProcessingNormapMap)
					{
						Property.CompressionSettings = TC_Normalmap;
					}
					UTexture2D* Texture = TextureFactory->CreateTexture(CurrentMaterial->GetOuter(), Property, CurrentMaterial->GetFlags());
					return { NewMaterialExpressionTextureObject(CurrentMaterial, Texture) };
				}
				else
				{
					LogMessages.Emplace(MDLImporterLogging::EMessageSeverity::Error, TEXT("Couldn't create texture."));
					return {};
				}
			}
			case mi::neuraylib::IValue::VK_BSDF_MEASUREMENT:
			{
				auto Handle = mi::base::make_handle(MDLConstant.get_interface<const mi::neuraylib::IValue_bsdf_measurement>());

				LogMessages.Emplace(MDLImporterLogging::EMessageSeverity::Error, TEXT("Measured BSDF or BTF textures aren't supported."));

				return {NewMaterialExpressionConstant(CurrentMaterial, 0.0f, 0.0f, 0.0f)};
			}
		}
		ensure(false);
		return {};
	}


}  // namespace Generator

#endif
