// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/Misc.h"

#include "DirectLink/DirectLinkLog.h"
#include "DirectLink/SceneSnapshot.h"
#include "DirectLink/ElementSnapshot.h"

#include "DatasmithSceneXmlWriter.h"
#include "IDatasmithSceneElements.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace DirectLink
{

const TCHAR* GetElementTypeName(const IDatasmithElement* Element)
{
	if (Element == nullptr)
	{
		return TEXT("<nullptr>");
	}
#define DS_ELEMENT_TYPE(x) \
	if (Element->IsA(x))   \
	{                      \
		return TEXT(#x);   \
	}
	DS_ELEMENT_TYPE(EDatasmithElementType::Variant)
	DS_ELEMENT_TYPE(EDatasmithElementType::Animation)
	DS_ELEMENT_TYPE(EDatasmithElementType::LevelSequence)
	DS_ELEMENT_TYPE(EDatasmithElementType::PostProcessVolume)
	DS_ELEMENT_TYPE(EDatasmithElementType::UEPbrMaterial)
	DS_ELEMENT_TYPE(EDatasmithElementType::Landscape)
	DS_ELEMENT_TYPE(EDatasmithElementType::Material)
	DS_ELEMENT_TYPE(EDatasmithElementType::CustomActor)
	DS_ELEMENT_TYPE(EDatasmithElementType::MetaData)
	DS_ELEMENT_TYPE(EDatasmithElementType::Scene)
	DS_ELEMENT_TYPE(EDatasmithElementType::PostProcess)
	DS_ELEMENT_TYPE(EDatasmithElementType::MaterialId)
	DS_ELEMENT_TYPE(EDatasmithElementType::Texture)
	DS_ELEMENT_TYPE(EDatasmithElementType::KeyValueProperty)
	DS_ELEMENT_TYPE(EDatasmithElementType::MasterMaterial)
	DS_ELEMENT_TYPE(EDatasmithElementType::BaseMaterial)
	DS_ELEMENT_TYPE(EDatasmithElementType::Shader)
	DS_ELEMENT_TYPE(EDatasmithElementType::Camera)
	DS_ELEMENT_TYPE(EDatasmithElementType::EnvironmentLight)
	DS_ELEMENT_TYPE(EDatasmithElementType::LightmassPortal)
	DS_ELEMENT_TYPE(EDatasmithElementType::AreaLight)
	DS_ELEMENT_TYPE(EDatasmithElementType::DirectionalLight)
	DS_ELEMENT_TYPE(EDatasmithElementType::SpotLight)
	DS_ELEMENT_TYPE(EDatasmithElementType::PointLight)
	DS_ELEMENT_TYPE(EDatasmithElementType::Light)
	DS_ELEMENT_TYPE(EDatasmithElementType::StaticMeshActor)
	DS_ELEMENT_TYPE(EDatasmithElementType::Actor)
	DS_ELEMENT_TYPE(EDatasmithElementType::HierarchicalInstanceStaticMesh)
	DS_ELEMENT_TYPE(EDatasmithElementType::StaticMesh)
	DS_ELEMENT_TYPE(EDatasmithElementType::None)
#undef DS_ELEMENT_TYPE
	return TEXT("<unknown>");
}


const FString& GetDumpPath()
{
	static const FString DumpPath = []() -> FString
	{
		const TCHAR* VarName = TEXT("DIRECTLINK_SNAPSHOT_PATH");
		FString Var = FPlatformMisc::GetEnvironmentVariable(VarName);
		FText Text;
		if (!FPaths::ValidatePath(Var, &Text))
		{
			UE_LOG(LogDirectLink, Warning, TEXT("Invalid path '%s' defined by environment variable %s (%s)."), *Var, *VarName, *Text.ToString());
			return FString();
		}
		return Var;
	}();
	return DumpPath;
}


void DumpDatasmithScene(const TSharedRef<IDatasmithScene>& Scene, const TCHAR* BaseName)
{
	const FString& DumpPath = GetDumpPath();
	if (DumpPath.IsEmpty())
	{
		return;
	}

	FString SceneIdStr;
	if (Scene->GetSharedState())
	{
		SceneIdStr = FString::Printf(TEXT(".%08X"), Scene->GetSharedState()->GetGuid().A);
	}
	FString FileName = DumpPath / BaseName + SceneIdStr + TEXT(".directlink.udatasmith");
	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*FileName));
	if (!Ar.IsValid())
	{
		return;
	}

	FDatasmithSceneXmlWriter Writer;
	Writer.Serialize(Scene, *Ar);
}


void DumpSceneSnapshot(FSceneSnapshot& SceneSnapshot, const FString& BaseFileName)
{
	const FString& DumpPath = GetDumpPath();
	if (DumpPath.IsEmpty())
	{
		return;
	}

	auto& Elements = SceneSnapshot.Elements;
	auto& SceneId = SceneSnapshot.SceneId;
	FString SceneIdStr = FString::Printf(TEXT(".%08X"), SceneId.SceneGuid.A);
	FString FileName = DumpPath / BaseFileName + SceneIdStr + TEXT(".directlink.scenesnap");
	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*FileName));
	if (!Ar.IsValid())
	{
		return;
	}

	auto Write = [&](const FString& Value)
	{
		FTCHARToUTF8 UTF8String( *Value );
		Ar->Serialize( (ANSICHAR*)UTF8String.Get(), UTF8String.Length() );
	};

	Elements.KeySort(TLess<FSceneGraphId>());

	Write(FString::Printf(TEXT("%d elements:\n"), Elements.Num()));

	for (const auto& KV : Elements)
	{
		Write(FString::Printf(
			TEXT("%d -> %08X (data:%08X ref:%08X)\n")
			, KV.Key
			, KV.Value->GetHash()
			, KV.Value->GetDataHash()
			, KV.Value->GetRefHash()
		));
	}
}


} // namespace DirectLink
