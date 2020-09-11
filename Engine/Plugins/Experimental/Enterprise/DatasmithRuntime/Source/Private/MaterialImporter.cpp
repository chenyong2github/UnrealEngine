// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneImporter.h"

#include "DatasmithRuntimeUtils.h"
#include "LogCategory.h"

#include "DatasmithMaterialElements.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"
#include "MasterMaterials/DatasmithMasterMaterial.h"
#include "MasterMaterials/DatasmithMasterMaterialManager.h"
#include "MasterMaterials/DatasmithMasterMaterialSelector.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureLightProfile.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace DatasmithRuntime
{
	void UpdateMaterials(TSet<FSceneGraphId>& MaterialElementSet, TMap< FSceneGraphId, FAssetData >& AssetDataList);

	TSharedPtr< IDatasmithElement > ValidatePbrMaterial( TSharedPtr< IDatasmithUEPbrMaterialElement > PbrMaterialElement, FSceneImporter& SceneImporter )
	{
		// Assuming Pbr materials using material attributes are layered materials
		// #ue_liveupdate: Revisit this logic
		if (PbrMaterialElement->GetUseMaterialAttributes())
		{
			for (int32 Index = 0; Index < PbrMaterialElement->GetExpressionsCount(); ++Index)
			{
				IDatasmithMaterialExpression* Expression = PbrMaterialElement->GetExpression(Index);
				if (Expression && Expression->IsA(EDatasmithMaterialExpressionType::FunctionCall))
				{
					IDatasmithMaterialExpressionFunctionCall* FunctionCall = static_cast<IDatasmithMaterialExpressionFunctionCall*>(Expression);
					TSharedPtr< IDatasmithElement > ElementPtr = SceneImporter.GetElementFromName(MaterialPrefix + FunctionCall->GetFunctionPathName());
					ensure(ElementPtr.IsValid() && ElementPtr->IsA( EDatasmithElementType::UEPbrMaterial ));

					return ElementPtr;
				}
			}
		}

		return PbrMaterialElement;
	}

	void FSceneImporter::ProcessMaterialData(FAssetData& MaterialData)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::ProcessMaterialData);

		if (MaterialData.bProcessed)
		{
			return;
		}

		TSharedPtr< IDatasmithElement >& Element = Elements[ MaterialData.ElementId ];

		FString MaterialName = FString(Element->GetLabel()) + TEXT("_LU_") + FString::FromInt(MaterialData.ElementId);

		if ( !MaterialData.Object.IsValid() )
		{
#ifdef ASSET_DEBUG
			MaterialName = FDatasmithUtils::SanitizeObjectName(MaterialName);
			UPackage* Package = CreatePackage(nullptr, *FPaths::Combine( TEXT("/Engine/Transient/LU"), MaterialName));
			MaterialData.Object = TStrongObjectPtr<UObject>( UMaterialInstanceDynamic::Create( nullptr, Package, *MaterialName) );
			MaterialData.Object->SetFlags(RF_Public);
#else
			MaterialData.Object = TStrongObjectPtr<UObject>( UMaterialInstanceDynamic::Create( nullptr, nullptr) );
#endif
		}
		else if(MaterialName != MaterialData.Object->GetName())
		{
			// #ue_liveupdate: Rename
		}

		if ( Element->IsA( EDatasmithElementType::UEPbrMaterial ) )
		{
			Element = ValidatePbrMaterial(StaticCastSharedPtr< IDatasmithUEPbrMaterialElement >(Element), *this);
		}

		FActionTaskFunction AssignTextureFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
		{
			if (UTextureLightProfile* TextureProfile = Cast<UTextureLightProfile>(Object))
			{
				return this->AssignProfileTexture(Referencer, TextureProfile);
			}

			return this->AssignTexture(Referencer, Cast<UTexture2D>(Object));
		};

		FTextureCallback TextureCallback;
		TextureCallback = [this, ElementId = MaterialData.ElementId, AssignTextureFunc](const FString& TextureNamePrefixed, int32 PropertyIndex)->void
		{
			if (FSceneGraphId* ElementIdPtr = this->AssetElementMapping.Find(TextureNamePrefixed))
			{
				this->ProcessTextureData(*ElementIdPtr);

				FTextureData& TextureData = this->TextureDataList[*ElementIdPtr];
				this->AddToQueue(NONASYNC_QUEUE, { AssignTextureFunc, *ElementIdPtr, true, { EDataType::Material, ElementId, (int8)PropertyIndex } });
			}
		};

		TSharedPtr< IDatasmithBaseMaterialElement > BaseMaterialElement = StaticCastSharedPtr< IDatasmithBaseMaterialElement >(Element);

		const FString Host = FDatasmithMasterMaterialManager::Get().GetHostFromString( SceneElement->GetHost() );

		if( Element->IsA( EDatasmithElementType::UEPbrMaterial ) )
		{
			MaterialData.Requirements = ProcessMaterialElement(static_cast<IDatasmithUEPbrMaterialElement*>(Element.Get()), TextureCallback);
		}
		else if( Element->IsA( EDatasmithElementType::MasterMaterial ) )
		{
			MaterialData.Requirements = ProcessMaterialElement(StaticCastSharedPtr<IDatasmithMasterMaterialElement>(Element), *Host, TextureCallback);
		}

		MaterialData.bProcessed = true;

		FActionTaskFunction TaskFunc = [this](UObject*, const FReferencer& Referencer) -> EActionResult::Type
		{
			return this->ProcessMaterial(Referencer.GetId());
		};

		AddToQueue(MATERIAL_QUEUE, { TaskFunc, {EDataType::Material, MaterialData.ElementId, 0 } });
		TasksToComplete |= EDatasmithRuntimeWorkerTask::MaterialCreate;

		MaterialElementSet.Add(MaterialData.ElementId);
	}

	EActionResult::Type FSceneImporter::ProcessMaterial(FSceneGraphId ElementId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::ProcessMaterial);

		FAssetData& MaterialData = AssetDataList[ ElementId ];

		TSharedPtr< IDatasmithElement >& Element = Elements[ ElementId ];

		UMaterialInstanceDynamic* MaterialInstance = MaterialData.GetObject<UMaterialInstanceDynamic>();

		if ( Element->IsA( EDatasmithElementType::Material ) )
		{
			// Not supported
		}
		else if ( Element->IsA( EDatasmithElementType::MasterMaterial ) )
		{

			TSharedPtr< IDatasmithMasterMaterialElement > MaterialElement = StaticCastSharedPtr< IDatasmithMasterMaterialElement >( Element );

			MaterialData.bCompleted = LoadMasterMaterial(MaterialInstance, MaterialElement, SceneElement->GetHost());
		}
		else if ( Element->IsA( EDatasmithElementType::UEPbrMaterial ) )
		{
			IDatasmithUEPbrMaterialElement* MaterialElement = static_cast< IDatasmithUEPbrMaterialElement* >( Element.Get() );

			MaterialData.bCompleted = LoadPbrMaterial(MaterialInstance, MaterialElement);
		}

		if (MaterialData.bCompleted == false)
		{
			MaterialData.Object = nullptr;
			MaterialData.bCompleted = true;

			return EActionResult::Failed;
		}

		return EActionResult::Succeeded;
	}

	EActionResult::Type FSceneImporter::AssignTexture(const FReferencer& Referencer, UTexture2D* Texture)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::AssignTexture);

		if (Texture)
		{
			const FSceneGraphId ElementId = Referencer.GetId();

			FAssetData& MaterialData = AssetDataList[ ElementId ];

			if (!MaterialData.bCompleted)
			{
				return EActionResult::Retry;
			}

			UMaterialInstanceDynamic* MaterialInstance = MaterialData.GetObject<UMaterialInstanceDynamic>();
			ensure(MaterialInstance);

			TSharedPtr< IDatasmithElement >& Element = Elements[ ElementId ];

			if ( Element->IsA( EDatasmithElementType::Material ) )
			{
				// Not supported
			}
			else if ( Element->IsA( EDatasmithElementType::MasterMaterial ) )
			{
				IDatasmithMasterMaterialElement* MaterialElement = static_cast< IDatasmithMasterMaterialElement* >( Element.Get() );

				const int32 PropertyIndex = Referencer.Slot;
				const TSharedPtr< IDatasmithKeyValueProperty > Property = MaterialElement->GetProperty(PropertyIndex);
				ensure(Property.IsValid());

				FName PropertyName(Property->GetName());
				MaterialInstance->SetTextureParameterValue(PropertyName, Texture);
#if WITH_EDITOR
				Texture->ClearFlags(RF_Public);
#endif
			}
			else if ( Element->IsA( EDatasmithElementType::UEPbrMaterial ) )
			{
				FName PropertyName(PbrTexturePropertyNames[Referencer.Slot]);
				MaterialInstance->SetTextureParameterValue(PropertyName, Texture);
#if WITH_EDITOR
				Texture->ClearFlags(RF_Public);
#endif
			}
		}

		ActionCounter.Increment();

		return EActionResult::Succeeded;
	}

	void UpdateMaterials(TSet<FSceneGraphId>& MaterialElementSet, TMap< FSceneGraphId, FAssetData >& AssetDataList)
	{
		FMaterialUpdateContext MaterialUpdateContext;

		for( FSceneGraphId ElementId : MaterialElementSet )
		{
			FAssetData& MaterialData = AssetDataList[ ElementId ];

			if (UMaterialInstanceDynamic* MaterialInstance = MaterialData.GetObject<UMaterialInstanceDynamic>())
			{
				MaterialUpdateContext.AddMaterialInstance( MaterialInstance );

#if WITH_EDITOR
				// If BlendMode override property has been changed, make sure this combination of the parent material is compiled
				if ( MaterialInstance->BasePropertyOverrides.bOverride_BlendMode == true )
				{
					MaterialInstance->ForceRecompileForRendering();
				}
				else
				{
					// If a switch is overridden, we need to recompile
					FStaticParameterSet StaticParameters;
					MaterialInstance->GetStaticParameterValues( StaticParameters );

					for ( FStaticSwitchParameter& Switch : StaticParameters.StaticSwitchParameters )
					{
						if ( Switch.bOverride )
						{
							MaterialInstance->ForceRecompileForRendering();
							break;
						}
					}
				}

				MaterialInstance->PreEditChange( nullptr );
				MaterialInstance->PostEditChange();
#endif
			}
		}
	}
} // End of namespace DatasmithRuntime