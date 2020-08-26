// Copyright Epic Games, Inc. All Rights Reserved.
#include "AssetImportDataHandler.h"

TSharedPtr<FAssetDataHandler> FAssetDataHandler::AssetDataHandlerInst;

FAssetDataHandler::FAssetDataHandler()
{
	
}

TSharedPtr<FAssetDataHandler> FAssetDataHandler::Get()
{
	if (!AssetDataHandlerInst.IsValid())
	{
		AssetDataHandlerInst = MakeShareable(new FAssetDataHandler);

	}
	return AssetDataHandlerInst;
}

TSharedPtr<FAssetsData> FAssetDataHandler::GetAssetsData(const FString& AssetsImportJson)
{
	FString ImportData = ConcatJsonString(AssetsImportJson);
	AssetsImportData = MakeShareable(new FAssetsData);	
	TSharedPtr<FJsonObject> ImportDataObject = DeserializeJson(ImportData);	
	TArray<TSharedPtr<FJsonValue> > AssetsImportDataArray = ImportDataObject->GetArrayField(TEXT("Assets"));
	for (TSharedPtr<FJsonValue> AssetDataObject : AssetsImportDataArray)
	{
		TSharedPtr<FAssetTypeData> ParsedAssetData = GetAssetData(AssetDataObject->AsObject());
		AssetsImportData->AllAssetsData.Add(ParsedAssetData);
	}	
	return AssetsImportData;
}



TSharedPtr<FAssetTypeData> FAssetDataHandler::GetAssetData(TSharedPtr<FJsonObject> AssetDataObject)
{
	TSharedPtr<FAssetTypeData> ParsedAssetData = MakeShareable(new FAssetTypeData);
	ParsedAssetData->AssetMetaInfo = GetAssetMetaData(AssetDataObject);	

	TArray<TSharedPtr<FJsonValue>> TextureDataArray = AssetDataObject->GetArrayField(TEXT("components"));	

	for (TSharedPtr<FJsonValue> TextureData : TextureDataArray)
	{
		ParsedAssetData->TextureComponents.Add(GetAssetTextureData(TextureData->AsObject()));
	}	

	TArray<TSharedPtr<FJsonValue>> MeshDataArray = AssetDataObject->GetArrayField(TEXT("meshList"));
	for (TSharedPtr<FJsonValue> MeshData : MeshDataArray)
	{
		ParsedAssetData->MeshList.Add(GetAssetMeshData(MeshData->AsObject()));
	}

	TArray<TSharedPtr<FJsonValue>> LodDataArray = AssetDataObject->GetArrayField(TEXT("lodList"));	
	for (TSharedPtr<FJsonValue> LodData : LodDataArray)
	{
		ParsedAssetData->LodList.Add(GetAssetLodData(LodData->AsObject()));
	}

	TArray<TSharedPtr<FJsonValue>> PackedDataArray = AssetDataObject->GetArrayField(TEXT("packedTextures"));
	for (TSharedPtr<FJsonValue> PackedData : PackedDataArray)
	{
		ParsedAssetData->PackedTextures.Add(GetPackedTextureData(PackedData->AsObject()));
	}

	if (ParsedAssetData->AssetMetaInfo->Type == TEXT("3dplant"))
	{
		TArray<TSharedPtr<FJsonValue>> BillboardDataArray = AssetDataObject->GetArrayField(TEXT("components-billboard"));
		if (BillboardDataArray.Num() > 0)
		{
			for (TSharedPtr<FJsonValue> BillboardData : BillboardDataArray)
			{
				ParsedAssetData->BillboardTextures.Add(GetBillboardData(BillboardData->AsObject()));
			}
		}

		//Meta tags array for use in Lod screen sizes
		TArray<TSharedPtr<FJsonValue>> MetaTagsArray = AssetDataObject->GetArrayField(TEXT("meta"));
		ParsedAssetData->PlantsLodScreenSizes = GetLodScreenSizes(MetaTagsArray);	

		//Get the Use Billboard tag.
		ParsedAssetData->AssetMetaInfo->bUseBillboardMaterial = GetBillboardMaterialSubtype(MetaTagsArray);

	}
	//Get the material ids for modular windows from json
	if (ParsedAssetData->AssetMetaInfo->Type == TEXT("3d"))
	{
		GetMulitpleMaterialIds(AssetDataObject->GetArrayField(TEXT("meta")), ParsedAssetData->AssetMetaInfo);
	}

	return ParsedAssetData;	
}

TSharedPtr<FAssetMetaData> FAssetDataHandler::GetAssetMetaData(TSharedPtr<FJsonObject> MetaDataObject)
{
	TSharedPtr<FAssetMetaData> ParsedMetaData = MakeShareable(new FAssetMetaData);

	ParsedMetaData->Category = MetaDataObject->GetStringField(TEXT("category"));
	ParsedMetaData->Type = MetaDataObject->GetStringField(TEXT("type"));
	ParsedMetaData->Id = MetaDataObject->GetStringField(TEXT("id"));
	ParsedMetaData->Name = MetaDataObject->GetStringField(TEXT("name"));
	ParsedMetaData->Path = MetaDataObject->GetStringField(TEXT("path"));
	ParsedMetaData->TextureFormat = MetaDataObject->GetStringField(TEXT("textureFormat"));
	ParsedMetaData->ActiveLOD = MetaDataObject->GetStringField(TEXT("activeLOD"));
	
	ParsedMetaData->ExportPath = MetaDataObject->GetStringField(TEXT("exportPath"));
	ParsedMetaData->NamingConvention = MetaDataObject->GetStringField(TEXT("namingConvention"));
	ParsedMetaData->FolderNamingConvention = MetaDataObject->GetStringField(TEXT("folderNamingConvention"));
	ParsedMetaData->Resolution = MetaDataObject->GetStringField(TEXT("resolution"));

	if (MetaDataObject->HasField(TEXT("minLOD")))
	{
		ParsedMetaData->MinLOD = MetaDataObject->GetStringField(TEXT("minLOD"));
	}
	else {
		ParsedMetaData->MinLOD = TEXT("");
	}
	

	TArray<TSharedPtr<FJsonValue>> TagsArray = MetaDataObject->GetArrayField(TEXT("tags"));
	for (TSharedPtr<FJsonValue> TagData : TagsArray)
	{
		ParsedMetaData->Tags.Add(TagData->AsString());
	}

	TArray<TSharedPtr<FJsonValue>> CategoriesArray = MetaDataObject->GetArrayField(TEXT("categories"));
	for (TSharedPtr<FJsonValue> CategoryData : CategoriesArray)
	{
		ParsedMetaData->Categories.Add(CategoryData->AsString());
	}

	return ParsedMetaData;
	
}

TSharedPtr< FAssetPackedTextures> FAssetDataHandler::GetPackedTextureData(TSharedPtr<FJsonObject> PackedDataObject)
{
	TArray<FString> ChannelKeys = { "Red", "Green","Blue","Alpha","Grayscale" };
	TSharedPtr<FAssetPackedTextures> ParsedPackedData = MakeShareable(new FAssetPackedTextures);
	ParsedPackedData->PackedTextureData = GetAssetTextureData(PackedDataObject);
	TSharedPtr<FJsonObject> ChannelsData = PackedDataObject->GetObjectField(TEXT("channelsData"));

	for (FString ChannelKey : ChannelKeys)
	{
		
		TArray<FString> ChannelValues;
		if (!ChannelsData->HasField(ChannelKey)) continue;
		TArray<TSharedPtr<FJsonValue>> ChannelKeyData = ChannelsData->GetArrayField(ChannelKey);
		for (TSharedPtr<FJsonValue> ChData : ChannelKeyData)
		{
			
			ChannelValues.Add(ChData->AsString());			
		}
		ParsedPackedData->ChannelData.Add(ChannelKey, ChannelValues);
	}

	return ParsedPackedData;
}

TSharedPtr<FAssetTextureData> FAssetDataHandler::GetAssetTextureData(TSharedPtr<FJsonObject> TextureDataObject)
{
	
	TSharedPtr<FAssetTextureData> ParsedTextureData = MakeShareable(new FAssetTextureData);
	ParsedTextureData->Format = TextureDataObject->GetStringField(TEXT("format"));
	ParsedTextureData->Type = TextureDataObject->GetStringField(TEXT("type"));
	ParsedTextureData->Resolution = TextureDataObject->GetStringField(TEXT("resolution"));
	ParsedTextureData->Name = GetAssetName(TextureDataObject->GetStringField(TEXT("name")));
	ParsedTextureData->NameOverride = TextureDataObject->GetStringField(TEXT("nameOverride"));
	ParsedTextureData->Path = TextureDataObject->GetStringField(TEXT("path"));

	return ParsedTextureData;
}



TSharedPtr<FAssetMeshData> FAssetDataHandler::GetAssetMeshData(TSharedPtr<FJsonObject> MeshDataObject)
{
	TSharedPtr<FAssetMeshData> ParsedMeshData = MakeShareable(new FAssetMeshData);
	ParsedMeshData->Format = MeshDataObject->GetStringField(TEXT("format"));
	ParsedMeshData->Type = MeshDataObject->GetStringField(TEXT("type"));
	ParsedMeshData->Resolution = MeshDataObject->GetStringField(TEXT("resolution"));
	ParsedMeshData->Name = GetAssetName(MeshDataObject->GetStringField(TEXT("name")));
	ParsedMeshData->NameOverride = MeshDataObject->GetStringField(TEXT("nameOverride"));
	ParsedMeshData->Path = MeshDataObject->GetStringField(TEXT("path"));

	return ParsedMeshData;
}

TSharedPtr<FAssetLodData> FAssetDataHandler::GetAssetLodData(TSharedPtr<FJsonObject> LodDataObject)
{
	TSharedPtr<FAssetLodData> ParsedLodData = MakeShareable(new FAssetLodData);
	ParsedLodData->Lod = LodDataObject->GetStringField(TEXT("lod"));
	ParsedLodData->Path = LodDataObject->GetStringField(TEXT("path"));
	ParsedLodData->Name = LodDataObject->GetStringField(TEXT("name"));
	ParsedLodData->NameOverride = LodDataObject->GetStringField(TEXT("nameOverride"));
	ParsedLodData->LodObjectName = LodDataObject->GetStringField(TEXT("lodObjectName"));
	ParsedLodData->Format = LodDataObject->GetStringField(TEXT("format"));
	ParsedLodData->Type = LodDataObject->GetStringField(TEXT("type"));

	return ParsedLodData;
}

TSharedPtr<FAssetBillboardData> FAssetDataHandler::GetBillboardData(TSharedPtr<FJsonObject> BillboardObject)
{
	TSharedPtr<FAssetBillboardData> ParsedBillboardData = MakeShareable(new FAssetBillboardData);
	ParsedBillboardData->Path = BillboardObject->GetStringField(TEXT("path"));
	ParsedBillboardData->Type = BillboardObject->GetStringField(TEXT("type"));
	return ParsedBillboardData;
}

TMap<FString, TMap<FString, float>> FAssetDataHandler::GetLodScreenSizes(TArray<TSharedPtr<FJsonValue>> MetaTagsArray)
{
	TMap<FString, TMap<FString, float>> LODScreenSizes;

	for (TSharedPtr<FJsonValue> MetaData : MetaTagsArray)
	{
		TSharedPtr<FJsonObject> MetaObject = MetaData->AsObject();
		
		if (MetaObject->GetStringField(TEXT("key")) == TEXT("lodDistance"))
		{
			TArray<TSharedPtr<FJsonValue>> ScreenSizesArray = MetaObject->GetArrayField(TEXT("value"));
			for (TSharedPtr<FJsonValue> ScreenSizes : ScreenSizesArray)
			{
				
				FString VariationKey = FString::FromInt(ScreenSizes->AsObject()->GetIntegerField(TEXT("variation")));
				VariationKey = TEXT("Var") + VariationKey;
				TArray<TSharedPtr<FJsonValue>> VariationSSArray = ScreenSizes->AsObject()->GetArrayField(TEXT("distance"));
				
				TMap<FString, float> VariationScreenData;				
				for (TSharedPtr<FJsonValue> VarScreenSize : VariationSSArray)
				{					
					FString Lod = TEXT("lod") + FString::FromInt(VarScreenSize->AsObject()->GetIntegerField(TEXT("lod")));
					float ScreenSize = VarScreenSize->AsObject()->GetNumberField(TEXT("lodDistance"));
					
					VariationScreenData.Add( Lod, ScreenSize);
				}
				
				LODScreenSizes.Add(VariationKey, VariationScreenData);
			}

			break;
		}
	}

	return LODScreenSizes;
}

bool FAssetDataHandler::GetBillboardMaterialSubtype(TArray<TSharedPtr<FJsonValue>> MetaTagsArray)
{
	for (TSharedPtr<FJsonValue> MetaData : MetaTagsArray)
	{
		TSharedPtr<FJsonObject> MetaObject = MetaData->AsObject();

		if (MetaObject->GetStringField(TEXT("key")) == TEXT("useBillboardMaterial"))
		{
			return MetaObject->GetBoolField(TEXT("value"));
			break;
		}
	}
	return false;
}

void FAssetDataHandler::GetMulitpleMaterialIds(TArray<TSharedPtr<FJsonValue>> MetaTagsArray, TSharedPtr<FAssetMetaData> AssetMetaInfo)
{
	for (TSharedPtr<FJsonValue> MetaData : MetaTagsArray)
	{
		TSharedPtr<FJsonObject> MetaObject = MetaData->AsObject();

		if (MetaObject->GetStringField(TEXT("key")) == TEXT("materialIds"))
		{
			TArray<TSharedPtr<FJsonValue>> MaterialIds = MetaObject->GetArrayField(TEXT("value"));
			for (TSharedPtr<FJsonValue> MasterialIDData : MaterialIds)
			{
				FString MaterialType = MasterialIDData->AsObject()->GetStringField(TEXT("material"));
				TArray<TSharedPtr<FJsonValue>> Ids = MasterialIDData->AsObject()->GetArrayField(TEXT("ids"));
				TArray<int8> MeshMaterialIds;
				for (TSharedPtr<FJsonValue> MeshMaterialId : Ids)
				{
					MeshMaterialIds.Add(MeshMaterialId->AsNumber());
				}

				AssetMetaInfo->MaterialTypes.Add(MaterialType, MeshMaterialIds);
			}
			AssetMetaInfo->bIsModularWindow = true;
			return;
			
		}
	}
	AssetMetaInfo->bIsModularWindow = false;
}


FString FAssetDataHandler::GetAssetName(const FString& AssetFileName)
{
	FString AssetName, AssetExtension;	
	AssetFileName.Split(TEXT("."), &AssetName, &AssetExtension);
	return AssetName;
}

FString FAssetDataHandler::ConcatJsonString(const FString& AssetsImportJson)
{
	FString StartString = TEXT("{\"Assets\":");
	FString EndString = TEXT("}");
	FString FinalString = StartString + AssetsImportJson + EndString;	
	return FinalString;
}