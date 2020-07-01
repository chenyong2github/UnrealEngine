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
	//UE_LOG(LogTemp, Error, TEXT("%s"), *ImportData);
	//ImportData = TEXT("{\"Assets\"}:[\"Hello\", \"There\"]}");

	AssetsImportData = MakeShareable(new FAssetsData);
	//TSharedPtr<FJsonObject> ImportDataObject = MakeShareable(new FJsonObject);

	TSharedPtr<FJsonObject> ImportDataObject = DeserializeJson(ImportData);

	// Print the values in the FJSonobject
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
	//UE_LOG(LogTemp, Error, TEXT("Source active lod %s"), *ParsedMetaData->ActiveLOD);
	//UE_LOG(LogTemp, Error, TEXT("Source active lod %s"), *MetaDataObject->GetStringField(TEXT("activeLOD")));
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
		//UE_LOG(LogTemp, Error, TEXT("Channel Key : %s"), *ChannelKey);
		TArray<FString> ChannelValues;
		if (!ChannelsData->HasField(ChannelKey)) continue;
		TArray<TSharedPtr<FJsonValue>> ChannelKeyData = ChannelsData->GetArrayField(ChannelKey);
		for (TSharedPtr<FJsonValue> ChData : ChannelKeyData)
		{
			//UE_LOG(LogTemp, Error, TEXT("Channel Data : %s"), *ChData->AsString());
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