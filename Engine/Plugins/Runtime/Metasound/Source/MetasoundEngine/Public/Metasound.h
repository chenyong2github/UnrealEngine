// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "EdGraph/EdGraph.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundRouter.h"
#include "MetasoundUObjectRegistry.h"
#include "Serialization/Archive.h"
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITORONLY_DATA
#include "Algo/Transform.h"
#endif // WITH_EDITORONLY_DATA

#include "Metasound.generated.h"


UCLASS(Abstract)
class METASOUNDENGINE_API UMetasoundEditorGraphBase : public UEdGraph
{
	GENERATED_BODY()

public:
	virtual bool IsEditorOnly() const override { return true; }
	virtual bool NeedsLoadForEditorGame() const override { return false; }

	virtual void SetSynchronizationRequired() PURE_VIRTUAL(UMetasoundEditorGraphBase::SetSynchronizationRequired, )
	virtual void RegisterGraphWithFrontend() PURE_VIRTUAL(UMetasoundEditorGraphBase::RegisterGraphWithFrontend(), )

#if WITH_EDITORONLY_DATA
	virtual void ClearVersionedOnLoad() PURE_VIRTUAL(UMetasoundEditorGraphBase::ClearVersionedOnLoad, )
	virtual bool GetVersionedOnLoad() const PURE_VIRTUAL(UMetasoundEditorGraphBase::GetVersionedOnLoad, return false; )
	virtual void SetVersionedOnLoad() PURE_VIRTUAL(UMetasoundEditorGraphBase::SetVersionedOnLoad, )
#endif // WITH_EDITORONLY_DATA
};


namespace Metasound
{
	namespace ConsoleVariables
	{
		static float BlockRate = 100.f;
	} // namespace ConsoleVariables

#if WITH_EDITOR
	template <typename TMetaSoundObject>
	void PostEditUndo(TMetaSoundObject& InMetaSound)
	{
		InMetaSound.SetSynchronizationRequired();
		if (UMetasoundEditorGraphBase* Graph = Cast<UMetasoundEditorGraphBase>(InMetaSound.GetGraph()))
		{
			Graph->RegisterGraphWithFrontend();
		}
	}
#endif // WITH_EDITOR

	template <typename TMetaSoundObject>
	void PreSaveAsset(TMetaSoundObject& InMetaSound, FObjectPreSaveContext InSaveContext)
	{
		using namespace Metasound::Frontend;

#if WITH_EDITORONLY_DATA
		if (UMetasoundEditorGraphBase* MetaSoundGraph = Cast<UMetasoundEditorGraphBase>(InMetaSound.GetGraph()))
		{
			// Cooked data must be deterministic, so do not call register graph as this can
			// initiate an auto-update and/or local registry data cache and modify serialized data.
			if (!InSaveContext.IsCooking())
			{
				MetaSoundGraph->RegisterGraphWithFrontend();
				MetaSoundGraph->SetSynchronizationRequired();
			}
		}
#endif // WITH_EDITORONLY_DATA
	}

	template <typename TMetaSoundObject>
	void SerializeToArchive(TMetaSoundObject& InMetaSound, FArchive& InArchive)
	{
		if (InArchive.IsLoading())
		{
			if (InMetaSound.VersionAsset())
			{
#if WITH_EDITORONLY_DATA
				if (UMetasoundEditorGraphBase* MetaSoundGraph = Cast<UMetasoundEditorGraphBase>(InMetaSound.GetGraph()))
				{
					MetaSoundGraph->SetVersionedOnLoad();
				}
#endif // WITH_EDITORONLY_DATA
			}
		}
	}

#if WITH_EDITORONLY_DATA
	template <typename TMetaSoundObject>
	void SetMetaSoundRegistryAssetClassInfo(TMetaSoundObject& InMetaSound, const Metasound::Frontend::FNodeClassInfo& InClassInfo)
	{
		using namespace Metasound;
		using namespace Metasound::Frontend;

		check(AssetTags::AssetClassID == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, AssetClassID));
		check(AssetTags::RegistryInputTypes == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, RegistryInputTypes));
		check(AssetTags::RegistryOutputTypes == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, RegistryOutputTypes));
		check(AssetTags::RegistryVersionMajor == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, RegistryVersionMajor));
		check(AssetTags::RegistryVersionMinor == GET_MEMBER_NAME_CHECKED(TMetaSoundObject, RegistryVersionMinor));

		bool bMarkDirty = InMetaSound.AssetClassID != InClassInfo.AssetClassID;
		bMarkDirty |= InMetaSound.RegistryVersionMajor != InClassInfo.Version.Major;
		bMarkDirty |= InMetaSound.RegistryVersionMinor != InClassInfo.Version.Minor;

		InMetaSound.AssetClassID = InClassInfo.AssetClassID;
		InMetaSound.RegistryVersionMajor = InClassInfo.Version.Major;
		InMetaSound.RegistryVersionMinor = InClassInfo.Version.Minor;

		{
			TArray<FString> InputTypes;
			Algo::Transform(InClassInfo.InputTypes, InputTypes, [](const FName& Name) { return Name.ToString(); });

			const FString TypeString = FString::Join(InputTypes, *AssetTags::ArrayDelim);
			bMarkDirty |= InMetaSound.RegistryInputTypes != TypeString;
			InMetaSound.RegistryInputTypes = TypeString;
		}

		{
			TArray<FString> OutputTypes;
			Algo::Transform(InClassInfo.OutputTypes, OutputTypes, [](const FName& Name) { return Name.ToString(); });

			const FString TypeString = FString::Join(OutputTypes, *AssetTags::ArrayDelim);
			bMarkDirty |= InMetaSound.RegistryOutputTypes != TypeString;
			InMetaSound.RegistryOutputTypes = TypeString;
		}
	}
#endif // WITH_EDITORONLY_DATA
} // namespace Metasound


/**
 * This asset type is used for Metasound assets that can only be used as nodes in other Metasound graphs.
 * Because of this, they contain no required inputs or outputs.
 */
UCLASS(hidecategories = object, BlueprintType)
class METASOUNDENGINE_API UMetaSound : public UObject, public FMetasoundAssetBase
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundFrontendDocument RootMetaSoundDocument;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UMetasoundEditorGraphBase* Graph;
#endif // WITH_EDITORONLY_DATA

public:
	UMetaSound(const FObjectInitializer& ObjectInitializer);

	UPROPERTY()
	TSet<FString> ReferencedAssetClassKeys;

	UPROPERTY()
	TSet<FSoftObjectPath> ReferenceAssetClassCache;

	UPROPERTY(AssetRegistrySearchable)
	FGuid AssetClassID;

#if WITH_EDITORONLY_DATA
	UPROPERTY(AssetRegistrySearchable)
	FString RegistryInputTypes;

	UPROPERTY(AssetRegistrySearchable)
	FString RegistryOutputTypes;

	UPROPERTY(AssetRegistrySearchable)
	int32 RegistryVersionMajor = 0;

	UPROPERTY(AssetRegistrySearchable)
	int32 RegistryVersionMinor = 0;

	// Sets Asset Registry Metadata associated with this MetaSound
	virtual void SetRegistryAssetClassInfo(const Metasound::Frontend::FNodeClassInfo& InClassInfo) override;

	// Returns document name (for editor purposes, and avoids making document public for edit
	// while allowing editor to reference directly)
	static FName GetDocumentPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(UMetaSound, RootMetaSoundDocument);
	}

	// Name to display in editors
	virtual FText GetDisplayName() const override;

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with UMetaSoundSource.
	virtual UEdGraph* GetGraph() override;
	virtual const UEdGraph* GetGraph() const override;
	virtual UEdGraph& GetGraphChecked() override;
	virtual const UEdGraph& GetGraphChecked() const override;

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with UMetaSoundSource.
	virtual void SetGraph(UEdGraph* InGraph) override
	{
		Graph = CastChecked<UMetasoundEditorGraphBase>(InGraph);
	}
#endif // #if WITH_EDITORONLY_DATA

#if WITH_EDITOR
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PostEditUndo() override;
#endif // WITH_EDITOR

	virtual void BeginDestroy() override;
	virtual void PreSave(FObjectPreSaveContext InSaveContext) override;
	virtual void Serialize(FArchive& InArchive) override;

	virtual TSet<FSoftObjectPath>& GetReferencedAssetClassCache() override;
	virtual const TSet<FSoftObjectPath>& GetReferencedAssetClassCache() const override;

	// Returns Asset Metadata associated with this MetaSound
	virtual Metasound::Frontend::FNodeClassInfo GetAssetClassInfo() const override;


	virtual bool ConformObjectDataToInterfaces() override { return false; }

	virtual const TSet<FString>& GetReferencedAssetClassKeys() const override
	{
		return ReferencedAssetClassKeys;
	}

	UObject* GetOwningAsset() override
	{
		return this;
	}

	const UObject* GetOwningAsset() const override
	{
		return this;
	}

protected:
	virtual void SetReferencedAssetClassKeys(TSet<Metasound::Frontend::FNodeRegistryKey>&& InKeys) override;

	Metasound::Frontend::FDocumentAccessPtr GetDocument() override
	{
		using namespace Metasound::Frontend;
		// Return document using FAccessPoint to inform the TAccessPtr when the 
		// object is no longer valid.
		return MakeAccessPtr<FDocumentAccessPtr>(RootMetaSoundDocument.AccessPoint, RootMetaSoundDocument);
	}

	Metasound::Frontend::FConstDocumentAccessPtr GetDocument() const override
	{
		using namespace Metasound::Frontend;
		// Return document using FAccessPoint to inform the TAccessPtr when the 
		// object is no longer valid.
		return MakeAccessPtr<FConstDocumentAccessPtr>(RootMetaSoundDocument.AccessPoint, RootMetaSoundDocument);
	}
};
