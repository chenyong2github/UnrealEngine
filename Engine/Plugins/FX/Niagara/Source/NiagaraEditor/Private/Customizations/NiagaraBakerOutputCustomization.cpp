// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraBakerOutputCustomization.h"
#include "NiagaraBakerOutputTexture2D.h"
#include "NiagaraBakerOutputVolumeTexture.h"

#include "Editor/EditorEngine.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "IDetailsView.h"

#define LOCTEXT_NAMESPACE "NiagaraBakerOutputCustomization"

namespace NiagaraBakerOutputCustomization
{

//////////////////////////////////////////////////////////////////////////

struct FNiagaraAssetPathDetailBuilder : public IDetailCustomNodeBuilder, public TSharedFromThis<FNiagaraAssetPathDetailBuilder>
{
public:
	FNiagaraAssetPathDetailBuilder(TSharedPtr<IPropertyHandle> InPropertyHandle)
		: PropertyHandle(InPropertyHandle)
	{
	}

	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override
	{
		TSharedPtr<SWidget> NameWidget = PropertyHandle->CreatePropertyNameWidget();
		TSharedPtr<SWidget> ValueWidget = PropertyHandle->CreatePropertyValueWidget();

		//if (bCanResetToDefault)
		//{
		//	TSharedPtr<SHorizontalBox> Container;
		//	SAssignNew(Container, SHorizontalBox)
		//		+ SHorizontalBox::Slot()
		//		.AutoWidth()
		//		[
		//			ValueWidget.ToSharedRef()
		//		]
		//	+ SHorizontalBox::Slot()
		//		[
		//			ParentProperty->CreateDefaultPropertyButtonWidgets()
		//		];

		//	ValueWidget = Container;
		//}

		FUIAction CopyAction;
		FUIAction PasteAction;
		PropertyHandle->CreateDefaultPropertyCopyPasteActions(CopyAction, PasteAction);
			NodeRow
			.NameContent()
			[
				NameWidget.ToSharedRef()
			]
			.ValueContent()
			.MinDesiredWidth(300.0f)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				[
					ValueWidget.ToSharedRef()
				]
				+SHorizontalBox::Slot()
				[
					SNew(STextBlock).Text(LOCTEXT("Ooooh", "Ooooh"))
				]
			]
			.CopyAction(CopyAction)
			.PasteAction(PasteAction);
	}

	virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override
	{
		//uint32 NumChildren = 0;
		//if (ParentProperty->GetNumChildren(NumChildren) != FPropertyAccess::Fail)
		//{
		//	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		//	{
		//		TSharedPtr<IPropertyHandle> ChildProperty = ParentProperty->GetChildHandle(ChildIndex);
		//		if (ChildProperty.IsValid() && ChildProperty->GetProperty()->GetFName() != MeshProperty->GetProperty()->GetFName())
		//		{
		//			ChildrenBuilder.AddProperty(ChildProperty.ToSharedRef())
		//				.ShouldAutoExpand(false);
		//		}
		//	}
		//}
	}

	virtual FName GetName() const override
	{
		static const FName NAME_NiagaraAssetPathDetailBuilder("NiagaraAssetPathDetailBuilder");
		return NAME_NiagaraAssetPathDetailBuilder;
	}

	//virtual void SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren) override
	//{
	//	OnRebuildChildren = InOnRegenerateChildren;
	//}

	virtual void Tick(float DeltaTime) override {}
	virtual bool RequiresTick() const override { return false; }
	virtual bool InitiallyCollapsed() const override { return true; }
	virtual TSharedPtr<IPropertyHandle> GetPropertyHandle() const override { return PropertyHandle; }

	//void Rebuild()
	//{
	//	OnRebuildChildren.ExecuteIfBound();
	//}

private:
	TSharedPtr<IPropertyHandle> PropertyHandle;
	//TSharedPtr<IPropertyHandle> MeshProperty;
	//FSimpleDelegate OnRebuildChildren;
	//bool bCanResetToDefault = true;
};

//////////////////////////////////////////////////////////////////////////

struct FNiagaraBakerOutputDetails : public IDetailCustomization
{
	static void FocusContentBrowserToAsset(const FString& AssetPath)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName);
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		TArray<FAssetData> AssetDatas;
		AssetRegistry.GetAssetsByPackageName(FName(*AssetPath), AssetDatas);

		TArray<UObject*> AssetObjects;
		AssetObjects.Reserve(AssetDatas.Num());
		for (const FAssetData& AssetData : AssetDatas)
		{
			if (UObject* AssetObject = AssetData.GetAsset())
			{
				AssetObjects.Add(AssetObject);
			}
		}

		if (AssetObjects.Num() > 0)
		{
			GEditor->SyncBrowserToObjects(AssetObjects);
		}
	}

	static void ExploreFolder(const FString& Folder)
	{
		if ( FPaths::DirectoryExists(Folder) )
		{
			FPlatformProcess::ExploreFolder(*Folder);
		}
	}

	static void BuildAssetPathWidget(IDetailCategoryBuilder& DetailCategory, TSharedRef<IPropertyHandle>& PropertyHandle, TAttribute<FText>::FGetter TooltipGetter, FOnClicked OnClicked)
	{
		DetailCategory.AddProperty(PropertyHandle)
			.CustomWidget()
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					PropertyHandle->CreatePropertyValueWidget()
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ToolTipText(TAttribute<FText>::Create(TooltipGetter))
					.OnClicked(OnClicked)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.Search")))
					]
				]
			];
	}
};

//////////////////////////////////////////////////////////////////////////

struct FNiagaraBakerOutputTexture2DDetails : public FNiagaraBakerOutputDetails
{
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraBakerOutputTexture2DDetails>();
	}

	static FText BrowseAtlasToolTipText(TWeakObjectPtr<UNiagaraBakerOutputTexture2D> WeakOutput)
	{
		UNiagaraBakerOutputTexture2D* Output = WeakOutput.Get();
		return FText::Format(LOCTEXT("BrowseAtlasToolTipFormat", "Browse to atlas asset '{0}'"), FText::FromString(Output ? Output->GetAssetPath(Output->AtlasAssetPathFormat, 0) : FString()));
	}

	static FReply BrowseToAtlas(TWeakObjectPtr<UNiagaraBakerOutputTexture2D> WeakOutput)
	{
		if ( UNiagaraBakerOutputTexture2D* Output = WeakOutput.Get() )
		{
			const FString AssetPath = Output->GetAssetPath(Output->AtlasAssetPathFormat, 0);
			FocusContentBrowserToAsset(AssetPath);
		}
		return FReply::Handled();
	}

	static FText BrowseFrameAssetsToolTipText(TWeakObjectPtr<UNiagaraBakerOutputTexture2D> WeakOutput)
	{
		UNiagaraBakerOutputTexture2D* Output = WeakOutput.Get();
		return FText::Format(LOCTEXT("BrowseFrameAssetsToolTipFormat", "Browse to frames assets '{0}'"), FText::FromString(Output ? Output->GetAssetPath(Output->FramesAssetPathFormat, 0) : FString()));
	}

	static FReply BrowseToFrameAssets(TWeakObjectPtr<UNiagaraBakerOutputTexture2D> WeakOutput)
	{
		if (UNiagaraBakerOutputTexture2D* Output = WeakOutput.Get())
		{
			const FString AssetPath = Output->GetAssetPath(Output->FramesAssetPathFormat, 0);
			FocusContentBrowserToAsset(AssetPath);
		}
		return FReply::Handled();
	}

	static FText BrowseExportAssetsToolTipText(TWeakObjectPtr<UNiagaraBakerOutputTexture2D> WeakOutput)
	{
		UNiagaraBakerOutputTexture2D* Output = WeakOutput.Get();
		return FText::Format(LOCTEXT("BrowseExportAssetsToolTipFormat", "Browse to exported assets '{0}'"), FText::FromString(Output ? Output->GetExportFolder(Output->FramesExportPathFormat, 0) : FString()));
	}

	static FReply BrowseToExportAssets(TWeakObjectPtr<UNiagaraBakerOutputTexture2D> WeakOutput)
	{
		if (UNiagaraBakerOutputTexture2D* Output = WeakOutput.Get())
		{
			const FString ExportFolder = Output->GetExportFolder(Output->FramesExportPathFormat, 0);
			ExploreFolder(ExportFolder);
		}
		return FReply::Handled();
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		// We only support customization on 1 object
		TArray<TWeakObjectPtr<UObject>> ObjectsCustomized;
		DetailBuilder.GetObjectsBeingCustomized(ObjectsCustomized);
		if (ObjectsCustomized.Num() != 1 || !ObjectsCustomized[0]->IsA<UNiagaraBakerOutputTexture2D>())
		{
			return;
		}
		TWeakObjectPtr<UNiagaraBakerOutputTexture2D> WeakOutput = CastChecked<UNiagaraBakerOutputTexture2D>(ObjectsCustomized[0]);

		static FName NAME_Settings("Settings");
		IDetailCategoryBuilder& DetailCategory = DetailBuilder.EditCategory(NAME_Settings);

		TArray<TSharedRef<IPropertyHandle>> CategoryProperties;
		DetailCategory.GetDefaultProperties(CategoryProperties);

		for (TSharedRef<IPropertyHandle>& PropertyHandle : CategoryProperties)
		{
			const FName PropertyName = PropertyHandle->GetProperty() ? PropertyHandle->GetProperty()->GetFName() : NAME_None;
			if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraBakerOutputTexture2D, AtlasAssetPathFormat))
			{
				BuildAssetPathWidget(
					DetailCategory, PropertyHandle,
					TAttribute<FText>::FGetter::CreateStatic(&FNiagaraBakerOutputTexture2DDetails::BrowseAtlasToolTipText, WeakOutput),
					FOnClicked::CreateStatic(&FNiagaraBakerOutputTexture2DDetails::BrowseToAtlas, WeakOutput)
				);
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraBakerOutputTexture2D, FramesAssetPathFormat))
			{
				BuildAssetPathWidget(
					DetailCategory, PropertyHandle,
					TAttribute<FText>::FGetter::CreateStatic(&FNiagaraBakerOutputTexture2DDetails::BrowseFrameAssetsToolTipText, WeakOutput),
					FOnClicked::CreateStatic(&FNiagaraBakerOutputTexture2DDetails::BrowseToFrameAssets, WeakOutput)
				);
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraBakerOutputTexture2D, FramesExportPathFormat))
			{
				BuildAssetPathWidget(
					DetailCategory, PropertyHandle,
					TAttribute<FText>::FGetter::CreateStatic(&FNiagaraBakerOutputTexture2DDetails::BrowseExportAssetsToolTipText, WeakOutput),
					FOnClicked::CreateStatic(&FNiagaraBakerOutputTexture2DDetails::BrowseToExportAssets, WeakOutput)
				);
			}
			else
			{
				DetailCategory.AddProperty(PropertyHandle);
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////////

struct NiagaraBakerOutputVolumeTexture : public FNiagaraBakerOutputDetails
{
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<NiagaraBakerOutputVolumeTexture>();
	}

	static FText BrowseAtlasToolTipText(TWeakObjectPtr<UNiagaraBakerOutputVolumeTexture> WeakOutput)
	{
		UNiagaraBakerOutputVolumeTexture* Output = WeakOutput.Get();
		return FText::Format(LOCTEXT("BrowseAtlasToolTipFormat", "Browse to atlas asset '{0}'"), FText::FromString(Output ? Output->GetAssetPath(Output->AtlasAssetPathFormat, 0) : FString()));
	}

	static FReply BrowseToAtlas(TWeakObjectPtr<UNiagaraBakerOutputVolumeTexture> WeakOutput)
	{
		if (UNiagaraBakerOutputVolumeTexture* Output = WeakOutput.Get() )
		{
			const FString AssetPath = Output->GetAssetPath(Output->AtlasAssetPathFormat, 0);
			FocusContentBrowserToAsset(AssetPath);
		}
		return FReply::Handled();
	}

	static FText BrowseFrameAssetsToolTipText(TWeakObjectPtr<UNiagaraBakerOutputVolumeTexture> WeakOutput)
	{
		UNiagaraBakerOutputVolumeTexture* Output = WeakOutput.Get();
		return FText::Format(LOCTEXT("BrowseFrameAssetsToolTipFormat", "Browse to frames assets '{0}'"), FText::FromString(Output ? Output->GetAssetPath(Output->FramesAssetPathFormat, 0) : FString()));
	}

	static FReply BrowseToFrameAssets(TWeakObjectPtr<UNiagaraBakerOutputVolumeTexture> WeakOutput)
	{
		if (UNiagaraBakerOutputVolumeTexture* Output = WeakOutput.Get())
		{
			const FString AssetPath = Output->GetAssetPath(Output->FramesAssetPathFormat, 0);
			FocusContentBrowserToAsset(AssetPath);
		}
		return FReply::Handled();
	}

	static FText BrowseExportAssetsToolTipText(TWeakObjectPtr<UNiagaraBakerOutputVolumeTexture> WeakOutput)
	{
		UNiagaraBakerOutputVolumeTexture* Output = WeakOutput.Get();
		return FText::Format(LOCTEXT("BrowseExportAssetsToolTipFormat", "Browse to exported assets '{0}'"), FText::FromString(Output ? Output->GetExportFolder(Output->FramesExportPathFormat, 0) : FString()));
	}

	static FReply BrowseToExportAssets(TWeakObjectPtr<UNiagaraBakerOutputVolumeTexture> WeakOutput)
	{
		if (UNiagaraBakerOutputVolumeTexture* Output = WeakOutput.Get())
		{
			const FString ExportFolder = Output->GetExportFolder(Output->FramesExportPathFormat, 0);
			ExploreFolder(ExportFolder);
		}
		return FReply::Handled();
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		// We only support customization on 1 object
		TArray<TWeakObjectPtr<UObject>> ObjectsCustomized;
		DetailBuilder.GetObjectsBeingCustomized(ObjectsCustomized);
		if (ObjectsCustomized.Num() != 1 || !ObjectsCustomized[0]->IsA<UNiagaraBakerOutputVolumeTexture>())
		{
			return;
		}
		TWeakObjectPtr<UNiagaraBakerOutputVolumeTexture> WeakOutput = CastChecked<UNiagaraBakerOutputVolumeTexture>(ObjectsCustomized[0]);

		static FName NAME_Settings("Settings");
		IDetailCategoryBuilder& DetailCategory = DetailBuilder.EditCategory(NAME_Settings);

		TArray<TSharedRef<IPropertyHandle>> CategoryProperties;
		DetailCategory.GetDefaultProperties(CategoryProperties);

		for (TSharedRef<IPropertyHandle>& PropertyHandle : CategoryProperties)
		{
			const FName PropertyName = PropertyHandle->GetProperty() ? PropertyHandle->GetProperty()->GetFName() : NAME_None;
			if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraBakerOutputVolumeTexture, AtlasAssetPathFormat))
			{
				BuildAssetPathWidget(
					DetailCategory, PropertyHandle,
					TAttribute<FText>::FGetter::CreateStatic(&NiagaraBakerOutputVolumeTexture::BrowseAtlasToolTipText, WeakOutput),
					FOnClicked::CreateStatic(&NiagaraBakerOutputVolumeTexture::BrowseToAtlas, WeakOutput)
				);
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraBakerOutputVolumeTexture, FramesAssetPathFormat))
			{
				BuildAssetPathWidget(
					DetailCategory, PropertyHandle,
					TAttribute<FText>::FGetter::CreateStatic(&NiagaraBakerOutputVolumeTexture::BrowseFrameAssetsToolTipText, WeakOutput),
					FOnClicked::CreateStatic(&NiagaraBakerOutputVolumeTexture::BrowseToFrameAssets, WeakOutput)
				);
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraBakerOutputVolumeTexture, FramesExportPathFormat))
			{
				BuildAssetPathWidget(
					DetailCategory, PropertyHandle,
					TAttribute<FText>::FGetter::CreateStatic(&NiagaraBakerOutputVolumeTexture::BrowseExportAssetsToolTipText, WeakOutput),
					FOnClicked::CreateStatic(&NiagaraBakerOutputVolumeTexture::BrowseToExportAssets, WeakOutput)
				);
			}
			else
			{
				DetailCategory.AddProperty(PropertyHandle);
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////////

void RegisterCustomization(IDetailsView* DetailsView)
{
	DetailsView->RegisterInstancedCustomPropertyLayout(
		UNiagaraBakerOutputTexture2D::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraBakerOutputTexture2DDetails::MakeInstance)
	);

	DetailsView->RegisterInstancedCustomPropertyLayout(
		UNiagaraBakerOutputVolumeTexture::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&NiagaraBakerOutputVolumeTexture::MakeInstance)
	);
}

} //NiagaraBakerOutputCustomization

#undef LOCTEXT_NAMESPACE
