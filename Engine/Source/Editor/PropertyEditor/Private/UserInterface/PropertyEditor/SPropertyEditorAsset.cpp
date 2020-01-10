// Copyright Epic Games, Inc. All Rights Reserved.

#include "UserInterface/PropertyEditor/SPropertyEditorAsset.h"
#include "Engine/Texture.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/LevelScriptActor.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Particles/ParticleSystem.h"
#include "UserInterface/PropertyEditor/PropertyEditorConstants.h"
#include "PropertyEditorHelpers.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "SAssetDropTarget.h"
#include "AssetRegistryModule.h"
#include "Engine/Selection.h"
#include "ObjectPropertyNode.h"
#include "PropertyHandleImpl.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "PropertyEditor"

DECLARE_DELEGATE( FOnCopy );
DECLARE_DELEGATE( FOnPaste );

// Helper to retrieve the correct property that has the applicable metadata.
static const FProperty* GetActualMetadataProperty(const FProperty* Property)
{
	if (FProperty* OuterProperty = Property->GetOwner<FProperty>())
	{
		if (OuterProperty->IsA<FArrayProperty>()
			|| OuterProperty->IsA<FSetProperty>()
			|| OuterProperty->IsA<FMapProperty>())
		{
			return OuterProperty;
		}
	}

	return Property;
}

// Helper to support both meta=(TagName) and meta=(TagName=true) syntaxes
static bool GetTagOrBoolMetadata(const FProperty* Property, const TCHAR* TagName, bool bDefault)
{
	bool bResult = bDefault;

	if (Property->HasMetaData(TagName))
	{
		bResult = true;

		const FString ValueString = Property->GetMetaData(TagName);
		if (!ValueString.IsEmpty())
		{
			if (ValueString == TEXT("true"))
			{
				bResult = true;
			}
			else if (ValueString == TEXT("false"))
			{
				bResult = false;
			}
		}
	}

	return bResult;
}

bool SPropertyEditorAsset::ShouldDisplayThumbnail(const FArguments& InArgs, const UClass* InObjectClass) const
{
	if (!InArgs._DisplayThumbnail || !InArgs._ThumbnailPool.IsValid())
	{
		return false;
	}

	bool bShowThumbnail = InObjectClass == nullptr || !InObjectClass->IsChildOf(AActor::StaticClass());
	
	// also check metadata for thumbnail & text display
	const FProperty* PropertyToCheck = nullptr;
	if (PropertyEditor.IsValid())
	{
		PropertyToCheck = PropertyEditor->GetProperty();
	}
	else if (PropertyHandle.IsValid())
	{
		PropertyToCheck = PropertyHandle->GetProperty();
	}

	if (PropertyToCheck != nullptr)
	{
		PropertyToCheck = GetActualMetadataProperty(PropertyToCheck);

		return GetTagOrBoolMetadata(PropertyToCheck, TEXT("DisplayThumbnail"), bShowThumbnail);
	}

	return bShowThumbnail;
}

void SPropertyEditorAsset::InitializeClassFilters(const FProperty* Property)
{
	if (Property == nullptr)
	{
		AllowedClassFilters.Add(ObjectClass);
		return;
	}

	// Account for the allowed classes specified in the property metadata
	const FProperty* MetadataProperty = GetActualMetadataProperty(Property);

	bExactClass = GetTagOrBoolMetadata(MetadataProperty, TEXT("ExactClass"), false);

	const FString AllowedClassesFilterString = MetadataProperty->GetMetaData(TEXT("AllowedClasses"));
	if (!AllowedClassesFilterString.IsEmpty())
	{
		TArray<FString> AllowedClassFilterNames;
		AllowedClassesFilterString.ParseIntoArray(AllowedClassFilterNames, TEXT(","), true);

		for (FString& ClassName : AllowedClassFilterNames)
		{
			// User can potentially list class names with leading or trailing whitespace
			ClassName.TrimStartAndEndInline();

			UClass* Class = FindObject<UClass>(ANY_PACKAGE, *ClassName);
			if (!Class)
			{
				Class = LoadObject<UClass>(nullptr, *ClassName);
			}

			if (Class)
			{
				// If the class is an interface, expand it to be all classes in memory that implement the class.
				if (Class->HasAnyClassFlags(CLASS_Interface))
				{
					for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
					{
						UClass* const ClassWithInterface = (*ClassIt);
						if (ClassWithInterface->ImplementsInterface(Class))
						{
							AllowedClassFilters.Add(ClassWithInterface);
						}
					}
				}
				else
				{
					AllowedClassFilters.Add(Class);
				}
			}
		}
	}
	
	if (AllowedClassFilters.Num() == 0)
	{
		// always add the object class to the filters
		AllowedClassFilters.Add(ObjectClass);
	}

	const FString DisallowedClassesFilterString = MetadataProperty->GetMetaData(TEXT("DisallowedClasses"));
	if (!DisallowedClassesFilterString.IsEmpty())
	{
		TArray<FString> DisallowedClassFilterNames;
		DisallowedClassesFilterString.ParseIntoArray(DisallowedClassFilterNames, TEXT(","), true);

		for (FString& ClassName : DisallowedClassFilterNames)
		{
			// User can potentially list class names with leading or trailing whitespace
			ClassName.TrimStartAndEndInline();

			UClass* Class = FindObject<UClass>(ANY_PACKAGE, *ClassName);
			if (!Class)
			{
				Class = LoadObject<UClass>(nullptr, *ClassName);
			}

			if (Class)
			{
				// If the class is an interface, expand it to be all classes in memory that implement the class.
				if (Class->HasAnyClassFlags(CLASS_Interface))
				{
					for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
					{
						UClass* const ClassWithInterface = (*ClassIt);
						if (ClassWithInterface->ImplementsInterface(Class))
						{
							DisallowedClassFilters.Add(ClassWithInterface);
						}
					}
				}
				else
				{
					DisallowedClassFilters.Add(Class);
				}
			}
		}
	}
}

void SPropertyEditorAsset::InitializeAssetDataTags(const FProperty* Property)
{
	if (Property == nullptr)
	{
		return;
	}

	const FProperty* MetadataProperty = GetActualMetadataProperty(Property);
	const FString DisallowedAssetDataTagsFilterString = MetadataProperty->GetMetaData(TEXT("DisallowedAssetDataTags"));
	if (!DisallowedAssetDataTagsFilterString.IsEmpty())
	{
		TArray<FString> DisallowedAssetDataTagsAndValues;
		DisallowedAssetDataTagsFilterString.ParseIntoArray(DisallowedAssetDataTagsAndValues, TEXT(","), true);

		for (const FString& TagAndOptionalValueString : DisallowedAssetDataTagsAndValues)
		{
			TArray<FString> TagAndOptionalValue;
			TagAndOptionalValueString.ParseIntoArray(TagAndOptionalValue, TEXT("="), true);
			size_t NumStrings = TagAndOptionalValue.Num();
			check((NumStrings == 1) || (NumStrings == 2)); // there should be a single '=' within a tag/value pair

			if (!DisallowedAssetDataTags.IsValid())
			{
				DisallowedAssetDataTags = MakeShared<FAssetDataTagMap>();
			}
			DisallowedAssetDataTags->Add(FName(*TagAndOptionalValue[0]), (NumStrings > 1) ? TagAndOptionalValue[1] : FString());
		}
	}
}

bool SPropertyEditorAsset::IsAssetAllowed(const FAssetData& InAssetData)
{
	if (DisallowedAssetDataTags.IsValid())
	{
		for (const auto& DisallowedTagAndValue : *DisallowedAssetDataTags.Get())
		{
			if (InAssetData.TagsAndValues.ContainsKeyValue(DisallowedTagAndValue.Key, DisallowedTagAndValue.Value))
			{
				return false;
			}
		}
	}
	return true;
}

// Awful hack to deal with UClass::FindCommonBase taking an array of non-const classes...
static TArray<UClass*> ConstCastClassArray(TArray<const UClass*>& Classes)
{
	TArray<UClass*> Result;
	for (const UClass* Class : Classes)
	{
		Result.Add(const_cast<UClass*>(Class));
	}

	return Result;
}

void SPropertyEditorAsset::Construct(const FArguments& InArgs, const TSharedPtr<FPropertyEditor>& InPropertyEditor)
{
	PropertyEditor = InPropertyEditor;
	PropertyHandle = InArgs._PropertyHandle;
	OwnerAssetDataArray = InArgs._OwnerAssetDataArray;
	OnSetObject = InArgs._OnSetObject;
	OnShouldFilterAsset = InArgs._OnShouldFilterAsset;
	ObjectPath = InArgs._ObjectPath;

	FProperty* Property = nullptr;
	if (PropertyEditor.IsValid())
	{
		Property = PropertyEditor->GetPropertyNode()->GetProperty();
	}
	else if (PropertyHandle.IsValid() && PropertyHandle->IsValidHandle())
	{
		Property = PropertyHandle->GetProperty();
	}

	ObjectClass = InArgs._Class != nullptr ? InArgs._Class : GetObjectPropertyClass(Property);
	bAllowClear = InArgs._AllowClear.IsSet() ? InArgs._AllowClear.GetValue() : (Property ? !(Property->PropertyFlags & CPF_NoClear) : true);

	InitializeAssetDataTags(Property);
	if (DisallowedAssetDataTags.IsValid())
	{
		// re-route the filter delegate to our own if we have our own asset data tags filter :
		OnShouldFilterAsset.BindLambda([this, AssetFilter = InArgs._OnShouldFilterAsset](const FAssetData& InAssetData)
		{
			if (IsAssetAllowed(InAssetData))
			{
				return AssetFilter.IsBound() ? AssetFilter.Execute(InAssetData) : false;
			}
			return true;
		});
	}

	InitializeClassFilters(Property);

	// Make the ObjectClass more specific if we only have one class filter
	// eg. if ObjectClass was set to Actor, but AllowedClasses="PointLight", we can limit it to PointLight immediately
	if (AllowedClassFilters.Num() == 1 && DisallowedClassFilters.Num() == 0)
	{
		ObjectClass = const_cast<UClass*>(AllowedClassFilters[0]);
	}
	else
	{
		ObjectClass = UClass::FindCommonBase(ConstCastClassArray(AllowedClassFilters));
	}

	bIsActor = ObjectClass->IsChildOf(AActor::StaticClass());

	if (InArgs._NewAssetFactories.IsSet())
	{
		NewAssetFactories = InArgs._NewAssetFactories.GetValue();
	}
	// If there are more allowed classes than just UObject 
	else if (AllowedClassFilters.Num() > 1 || !AllowedClassFilters.Contains(UObject::StaticClass()))
	{
		NewAssetFactories = PropertyCustomizationHelpers::GetNewAssetFactoriesForClasses(AllowedClassFilters, DisallowedClassFilters);
	}
	
	TSharedPtr<SHorizontalBox> ValueContentBox = nullptr;
	ChildSlot
	[
		SNew( SAssetDropTarget )
		.OnIsAssetAcceptableForDrop( this, &SPropertyEditorAsset::OnAssetDraggedOver )
		.OnAssetDropped( this, &SPropertyEditorAsset::OnAssetDropped )
		[
			SAssignNew( ValueContentBox, SHorizontalBox )	
		]
	];

	TAttribute<bool> IsEnabledAttribute(this, &SPropertyEditorAsset::CanEdit);
	TAttribute<FText> TooltipAttribute(this, &SPropertyEditorAsset::OnGetToolTip);

	if (Property)
	{
		const FProperty* PropToConsider = GetActualMetadataProperty(Property);
		if (PropToConsider->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnTemplate))
		{
			// There are some cases where editing an Actor Property is not allowed, such as when it is contained within a struct or a CDO
			TArray<UObject*> ObjectList;
			if (PropertyEditor.IsValid())
			{
				PropertyEditor->GetPropertyHandle()->GetOuterObjects(ObjectList);
			}

			// If there is no objects, that means we must have a struct asset managing this property
			if (ObjectList.Num() == 0)
			{
				IsEnabledAttribute.Set(false);
				TooltipAttribute.Set(LOCTEXT("VariableHasDisableEditOnTemplate", "Editing this value in structure's defaults is not allowed"));
			}
			else
			{
				// Go through all the found objects and see if any are a CDO, we can't set an actor in a CDO default.
				for (UObject* Obj : ObjectList)
				{
					if (Obj->IsTemplate() && !Obj->IsA<ALevelScriptActor>())
					{
						IsEnabledAttribute.Set(false);
						TooltipAttribute.Set(LOCTEXT("VariableHasDisableEditOnTemplateTooltip", "Editing this value in a Class Default Object is not allowed"));
						break;
					}

				}
			}
		}
	}

	bool bOldEnableAttribute = IsEnabledAttribute.Get();
	if (bOldEnableAttribute && !InArgs._EnableContentPicker)
	{
		IsEnabledAttribute.Set(false);
	}

	AssetComboButton = SNew(SComboButton)
		.ToolTipText(TooltipAttribute)
		.ButtonStyle( FEditorStyle::Get(), "PropertyEditor.AssetComboStyle" )
		.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
		.OnGetMenuContent( this, &SPropertyEditorAsset::OnGetMenuContent )
		.OnMenuOpenChanged( this, &SPropertyEditorAsset::OnMenuOpenChanged )
		.IsEnabled( IsEnabledAttribute )
		.ContentPadding(2.0f)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image( this, &SPropertyEditorAsset::GetStatusIcon )
			]

			+SHorizontalBox::Slot()
			.FillWidth(1)
			.VAlign(VAlign_Center)
			[
				// Show the name of the asset or actor
				SNew(STextBlock)
				.TextStyle( FEditorStyle::Get(), "PropertyEditor.AssetClass" )
				.Font( FEditorStyle::GetFontStyle( PropertyEditorConstants::PropertyFontStyle ) )
				.Text(this,&SPropertyEditorAsset::OnGetAssetName)
			]
		];

	if (bOldEnableAttribute && !InArgs._EnableContentPicker)
	{
		IsEnabledAttribute.Set(true);
	}

	TSharedPtr<SWidget> ButtonBoxWrapper;
	TSharedRef<SHorizontalBox> ButtonBox = SNew( SHorizontalBox );
	
	TSharedPtr<SVerticalBox> CustomContentBox;

	if(ShouldDisplayThumbnail(InArgs, ObjectClass))
	{
		FObjectOrAssetData Value; 
		GetValue( Value );

		AssetThumbnail = MakeShareable( new FAssetThumbnail( Value.AssetData, InArgs._ThumbnailSize.X, InArgs._ThumbnailSize.Y, InArgs._ThumbnailPool ) );

		FAssetThumbnailConfig AssetThumbnailConfig;
		TSharedPtr<IAssetTypeActions> AssetTypeActions;
		if (ObjectClass != nullptr)
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(ObjectClass).Pin();

			if (AssetTypeActions.IsValid())
			{
				AssetThumbnailConfig.AssetTypeColorOverride = AssetTypeActions->GetTypeColor();
			}
		}

		ValueContentBox->AddSlot()
		.Padding( 0.0f, 0.0f, 2.0f, 0.0f )
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew( ThumbnailBorder, SBorder )
				.Padding( 5.0f )
				.BorderImage( this, &SPropertyEditorAsset::GetThumbnailBorder )
				.OnMouseDoubleClick( this, &SPropertyEditorAsset::OnAssetThumbnailDoubleClick )
				[
					SNew( SBox )
					.ToolTipText(TooltipAttribute)
					.WidthOverride( InArgs._ThumbnailSize.X ) 
					.HeightOverride( InArgs._ThumbnailSize.Y )
					[
						AssetThumbnail->MakeThumbnailWidget(AssetThumbnailConfig)
					]
				]
			]
		];

		if(InArgs._DisplayCompactSize)
		{
			ValueContentBox->AddSlot()
			[
				SNew( SBox )
				.VAlign( VAlign_Center )
				[
					SAssignNew( CustomContentBox, SVerticalBox )
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							AssetComboButton.ToSharedRef()
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SAssignNew(ButtonBoxWrapper, SBox)
							.Padding(FMargin(0.0f, 2.0f, 4.0f, 2.0f))
							[
								ButtonBox
							]
						]
					]
				]
			];
		}
		else
		{
			ValueContentBox->AddSlot()
			[
				SNew( SBox )
				.VAlign( VAlign_Center )
				[
					SAssignNew( CustomContentBox, SVerticalBox )
					+ SVerticalBox::Slot()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						AssetComboButton.ToSharedRef()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(ButtonBoxWrapper, SBox)
						.Padding( FMargin( 0.0f, 2.0f, 4.0f, 2.0f ) )
						[
							ButtonBox
						]
					]
				]
			];
		}
	}
	else
	{
		ValueContentBox->AddSlot()
		[
			SAssignNew( CustomContentBox, SVerticalBox )
			+SVerticalBox::Slot()
			.VAlign( VAlign_Center )
			[
				SNew( SHorizontalBox )
				+ SHorizontalBox::Slot()
				[
					AssetComboButton.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SAssignNew(ButtonBoxWrapper, SBox)
					.Padding( FMargin( 4.f, 0.f ) )
					[
						ButtonBox
					]
				]
			]
		];
	}

	if( InArgs._CustomContentSlot.Widget != SNullWidget::NullWidget )
	{
		CustomContentBox->AddSlot()
		.VAlign( VAlign_Center )
		.Padding( FMargin( 0.0f, 2.0f ) )
		[
			InArgs._CustomContentSlot.Widget
		];
	}

	if( !bIsActor && InArgs._DisplayUseSelected )
	{
		ButtonBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding( 2.0f, 0.0f )
		[
			PropertyCustomizationHelpers::MakeUseSelectedButton( FSimpleDelegate::CreateSP( this, &SPropertyEditorAsset::OnUse ), FText(), IsEnabledAttribute )
		];
	}

	if( InArgs._DisplayBrowse )
	{
		ButtonBox->AddSlot()
		.Padding( 2.0f, 0.0f )
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			PropertyCustomizationHelpers::MakeBrowseButton(
				FSimpleDelegate::CreateSP( this, &SPropertyEditorAsset::OnBrowse ),
				TAttribute<FText>( this, &SPropertyEditorAsset::GetOnBrowseToolTip )
				)
		];
	}

	if( bIsActor )
	{
		TSharedRef<SWidget> ActorPicker = PropertyCustomizationHelpers::MakeInteractiveActorPicker( FOnGetAllowedClasses::CreateSP(this, &SPropertyEditorAsset::OnGetAllowedClasses), FOnShouldFilterActor(), FOnActorSelected::CreateSP( this, &SPropertyEditorAsset::OnActorSelected ) );
		ActorPicker->SetEnabled( IsEnabledAttribute );

		ButtonBox->AddSlot()
		.Padding( 2.0f, 0.0f )
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			ActorPicker
		];
	}

	if(InArgs._ResetToDefaultSlot.Widget != SNullWidget::NullWidget )
	{
		TSharedRef<SWidget> ResetToDefaultWidget  = InArgs._ResetToDefaultSlot.Widget;
		ResetToDefaultWidget->SetEnabled( IsEnabledAttribute );

		ButtonBox->AddSlot()
		.Padding( 4.0f, 0.0f )
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			ResetToDefaultWidget
		];
	}

	if (ButtonBoxWrapper.IsValid())
	{
		ButtonBoxWrapper->SetVisibility(ButtonBox->NumSlots() > 0 ? EVisibility::Visible : EVisibility::Collapsed);
	}
}

void SPropertyEditorAsset::GetDesiredWidth( float& OutMinDesiredWidth, float &OutMaxDesiredWidth )
{
	OutMinDesiredWidth = 250.f;
	// No max width
	OutMaxDesiredWidth = 350.f;
}

const FSlateBrush* SPropertyEditorAsset::GetThumbnailBorder() const
{
	if ( ThumbnailBorder->IsHovered() )
	{
		return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailLight");
	}
	else
	{
		return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailShadow");
	}
}

const FSlateBrush* SPropertyEditorAsset::GetStatusIcon() const
{
	static FSlateNoResource EmptyBrush = FSlateNoResource();

	EActorReferenceState State = GetActorReferenceState();

	if (State == EActorReferenceState::Unknown)
	{
		return FEditorStyle::GetBrush("Icons.Warning");
	}
	else if (State == EActorReferenceState::Error)
	{
		return FEditorStyle::GetBrush("Icons.Error");
	}

	return &EmptyBrush;
}

SPropertyEditorAsset::EActorReferenceState SPropertyEditorAsset::GetActorReferenceState() const
{
	if (bIsActor)
	{
		FObjectOrAssetData Value;
		GetValue(Value);

		if (Value.Object != nullptr)
		{
			// If this is not an actual actor, this is broken
			if (!Value.Object->IsA(AActor::StaticClass()))
			{
				return EActorReferenceState::Error;
			}

			return EActorReferenceState::Loaded;
		}
		else if (Value.ObjectPath.IsNull())
		{
			return EActorReferenceState::Null;
		}
		else
		{
			// Get a path pointing to the owning map
			FSoftObjectPath MapObjectPath = FSoftObjectPath(Value.ObjectPath.GetAssetPathName(), FString());

			if (MapObjectPath.ResolveObject())
			{
				// If the map is valid but the object is not
				return EActorReferenceState::Error;
			}

			return EActorReferenceState::Unknown;
		}
	}
	return EActorReferenceState::NotAnActor;
}

void SPropertyEditorAsset::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if( AssetThumbnail.IsValid() && !GIsSavingPackage && !IsGarbageCollecting() )
	{
		// Ensure the thumbnail is up to date
		FObjectOrAssetData Value;
		GetValue( Value );

		// If the thumbnail is not the same as the object value set the thumbnail to the new value
		if( !(AssetThumbnail->GetAssetData() == Value.AssetData) )
		{
			AssetThumbnail->SetAsset( Value.AssetData );
		}
	}
}

bool SPropertyEditorAsset::Supports( const TSharedRef< FPropertyEditor >& InPropertyEditor )
{
	const TSharedRef< FPropertyNode > PropertyNode = InPropertyEditor->GetPropertyNode();
	if(	PropertyNode->HasNodeFlags(EPropertyNodeFlags::EditInlineNew) )
	{
		return false;
	}

	return Supports(PropertyNode->GetProperty());
}

bool SPropertyEditorAsset::Supports( const FProperty* NodeProperty )
{
	const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>( NodeProperty );
	const FInterfaceProperty* InterfaceProperty = CastField<const FInterfaceProperty>( NodeProperty );

	if ( ( ObjectProperty != nullptr || InterfaceProperty != nullptr )
		 && !NodeProperty->IsA(FClassProperty::StaticClass()) 
		 && !NodeProperty->IsA(FSoftClassProperty::StaticClass()) )
	{
		return true;
	}
	
	return false;
}

TSharedRef<SWidget> SPropertyEditorAsset::OnGetMenuContent()
{
	FObjectOrAssetData Value;
	GetValue(Value);

	if(bIsActor)
	{
		return PropertyCustomizationHelpers::MakeActorPickerWithMenu(Cast<AActor>(Value.Object),
																	 bAllowClear,
																	 FOnShouldFilterActor::CreateSP( this, &SPropertyEditorAsset::IsFilteredActor ),
																	 FOnActorSelected::CreateSP( this, &SPropertyEditorAsset::OnActorSelected),
																	 FSimpleDelegate::CreateSP( this, &SPropertyEditorAsset::CloseComboButton ),
																	 FSimpleDelegate::CreateSP( this, &SPropertyEditorAsset::OnUse ) );
	}
	else
	{
		return PropertyCustomizationHelpers::MakeAssetPickerWithMenu(Value.AssetData,
																	 bAllowClear,
																	 AllowedClassFilters,
																	 DisallowedClassFilters,
																	 NewAssetFactories,
																	 OnShouldFilterAsset,
																	 FOnAssetSelected::CreateSP(this, &SPropertyEditorAsset::OnAssetSelected),
																	 FSimpleDelegate::CreateSP(this, &SPropertyEditorAsset::CloseComboButton),
																	 GetMostSpecificPropertyHandle(),
																	 OwnerAssetDataArray);
	}
}

void SPropertyEditorAsset::OnMenuOpenChanged(bool bOpen)
{
	if ( bOpen == false )
	{
		AssetComboButton->SetMenuContent(SNullWidget::NullWidget);
	}
}

bool SPropertyEditorAsset::IsFilteredActor( const AActor* const Actor ) const
{
	bool IsAllowed = Actor->IsA(ObjectClass) && !Actor->IsChildActor() && IsClassAllowed(Actor->GetClass());
	return IsAllowed;
}

void SPropertyEditorAsset::CloseComboButton()
{
	AssetComboButton->SetIsOpen(false);
}

FText SPropertyEditorAsset::OnGetAssetName() const
{
	FObjectOrAssetData Value; 
	FPropertyAccess::Result Result = GetValue( Value );

	FText Name = LOCTEXT("None", "None");
	if( Result == FPropertyAccess::Success )
	{
		if(Value.Object != nullptr)
		{
			if( bIsActor )
			{
				AActor* Actor = Cast<AActor>(Value.Object);

				if (Actor)
				{
					Name = FText::AsCultureInvariant(Actor->GetActorLabel());
				}
				else
				{
					Name = FText::AsCultureInvariant(Value.Object->GetName());
				}
			}
			else if (UField* AsField = Cast<UField>(Value.Object))
			{
				Name = AsField->GetDisplayNameText();
			}
			else
			{
				Name = FText::AsCultureInvariant(Value.Object->GetName());
			}
		}
		else if( Value.AssetData.IsValid() )
		{
			Name = FText::AsCultureInvariant(Value.AssetData.AssetName.ToString());
		}
		else if (Value.ObjectPath.IsValid())
		{
			Name = FText::AsCultureInvariant(Value.ObjectPath.ToString());
		}
	}
	else if( Result == FPropertyAccess::MultipleValues )
	{
		Name = LOCTEXT("MultipleValues", "Multiple Values");
	}

	return Name;
}

FText SPropertyEditorAsset::OnGetAssetClassName() const
{
	UClass* Class = GetDisplayedClass();
	if(Class)
	{
		return FText::AsCultureInvariant(Class->GetName());
	}
	return FText::GetEmpty();
}

FText SPropertyEditorAsset::OnGetToolTip() const
{
	FObjectOrAssetData Value; 
	FPropertyAccess::Result Result = GetValue( Value );

	FText ToolTipText = FText::GetEmpty();

	if( Result == FPropertyAccess::Success )
	{
		if ( bIsActor )
		{
			// Always show full path instead of label
			EActorReferenceState State = GetActorReferenceState();
			FFormatNamedArguments Args;
			Args.Add(TEXT("Actor"), FText::AsCultureInvariant(Value.ObjectPath.ToString()));
			if (State == EActorReferenceState::Null)
			{
				ToolTipText = LOCTEXT("EmptyActorReference", "None");
			}
			else if (State == EActorReferenceState::Error)
			{
				ToolTipText = FText::Format(LOCTEXT("BrokenActorReference", "Broken reference to Actor ID '{Actor}', it was deleted or renamed"), Args);
			}
			else if (State == EActorReferenceState::Unknown)
			{
				ToolTipText = FText::Format(LOCTEXT("UnknownActorReference", "Unloaded reference to Actor ID '{Actor}', use Browse to load level"), Args);
			}
			else
			{
				ToolTipText = FText::Format(LOCTEXT("GoodActorReference", "Reference to Actor ID '{Actor}'"), Args);
			}
		}
		else if( Value.Object != nullptr )
		{
			// Display the package name which is a valid path to the object without redundant information
			ToolTipText = FText::AsCultureInvariant(Value.Object->GetOutermost()->GetName());
		}
		else if( Value.AssetData.IsValid() )
		{
			ToolTipText = FText::AsCultureInvariant(Value.AssetData.PackageName.ToString());
		}
	}
	else if( Result == FPropertyAccess::MultipleValues )
	{
		ToolTipText = LOCTEXT("MultipleValues", "Multiple Values");
	}

	if( ToolTipText.IsEmpty() )
	{
		ToolTipText = FText::AsCultureInvariant(ObjectPath.Get());
	}

	return ToolTipText;
}

void SPropertyEditorAsset::SetValue( const FAssetData& AssetData )
{
	AssetComboButton->SetIsOpen(false);

	if(CanSetBasedOnCustomClasses(AssetData))
	{
		FText AssetReferenceFilterFailureReason;
		if (CanSetBasedOnAssetReferenceFilter(AssetData, &AssetReferenceFilterFailureReason))
		{
			if (PropertyEditor.IsValid())
			{
				PropertyEditor->GetPropertyHandle()->SetValue(AssetData);
			}

			OnSetObject.ExecuteIfBound(AssetData);
		}
		else if (!AssetReferenceFilterFailureReason.IsEmpty())
		{
			FNotificationInfo Info(AssetReferenceFilterFailureReason);
			Info.ExpireDuration = 4.f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}
}

FPropertyAccess::Result SPropertyEditorAsset::GetValue( FObjectOrAssetData& OutValue ) const
{
	// Potentially accessing the value while garbage collecting or saving the package could trigger a crash.
	// so we fail to get the value when that is occurring.
	if ( GIsSavingPackage || IsGarbageCollecting() )
	{
		return FPropertyAccess::Fail;
	}

	FPropertyAccess::Result Result = FPropertyAccess::Fail;

	if( PropertyEditor.IsValid() && PropertyEditor->GetPropertyHandle()->IsValidHandle() )
	{
		UObject* Object = nullptr;
		Result = PropertyEditor->GetPropertyHandle()->GetValue(Object);

		if (Object == nullptr)
		{
			// Check to see if it's pointing to an unloaded object
			FString CurrentObjectPath;
			PropertyEditor->GetPropertyHandle()->GetValueAsFormattedString( CurrentObjectPath );

			if (CurrentObjectPath.Len() > 0 && CurrentObjectPath != TEXT("None"))
			{
				FSoftObjectPath SoftObjectPath = FSoftObjectPath(CurrentObjectPath);

				if (SoftObjectPath.IsAsset())
				{
					if (!CachedAssetData.IsValid() || CachedAssetData.ObjectPath.ToString() != CurrentObjectPath)
					{
						static FName AssetRegistryName("AssetRegistry");

						FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(AssetRegistryName);
						CachedAssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*CurrentObjectPath);
					}

					Result = FPropertyAccess::Success;
					OutValue = FObjectOrAssetData(CachedAssetData);
				}
				else
				{
					// This is an actor or other subobject reference
					if (CachedAssetData.IsValid())
					{
						CachedAssetData = FAssetData();
					}

					Result = FPropertyAccess::Success;
					OutValue = FObjectOrAssetData(SoftObjectPath);
				}

				return Result;
			}
		}

#if !UE_BUILD_SHIPPING
		if (Object && !Object->IsValidLowLevel())
		{
			const FProperty* Property = PropertyEditor->GetProperty();
			UE_LOG(LogPropertyNode, Fatal, TEXT("Property \"%s\" (%s) contains invalid data."), *Property->GetName(), *Property->GetCPPType());
		}
#endif

		OutValue = FObjectOrAssetData( Object );
	}
	else
	{
		FSoftObjectPath SoftObjectPath;
		UObject* Object = nullptr;
		if (PropertyHandle.IsValid())
		{
			Result = PropertyHandle->GetValue(Object);
		}
		else
		{
			SoftObjectPath = FSoftObjectPath(ObjectPath.Get());
			Object = SoftObjectPath.ResolveObject();

			if (Object != nullptr)
			{
				Result = FPropertyAccess::Success;
			}
		}

		if (Object != nullptr)
		{
#if !UE_BUILD_SHIPPING
			if (!Object->IsValidLowLevel())
			{
				const FProperty* Property = PropertyEditor->GetProperty();
				UE_LOG(LogPropertyNode, Fatal, TEXT("Property \"%s\" (%s) contains invalid data."), *Property->GetName(), *Property->GetCPPType());
			}
#endif

			OutValue = FObjectOrAssetData(Object);
		}
		else
		{
			if (SoftObjectPath.IsNull())
			{
				SoftObjectPath = FSoftObjectPath(ObjectPath.Get());
			}

			if (SoftObjectPath.IsAsset())
			{
				const FString CurrentObjectPath = SoftObjectPath.ToString();
				if (CurrentObjectPath != TEXT("None") && (!CachedAssetData.IsValid() || CachedAssetData.ObjectPath.ToString() != CurrentObjectPath))
				{
					static FName AssetRegistryName("AssetRegistry");

					FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(AssetRegistryName);
					CachedAssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*CurrentObjectPath);
				}

				OutValue = FObjectOrAssetData(CachedAssetData);
				Result = FPropertyAccess::Success;
			}
			else
			{
				// This is an actor or other subobject reference
				if (CachedAssetData.IsValid())
				{
					CachedAssetData = FAssetData();
				}

				OutValue = FObjectOrAssetData(SoftObjectPath);
			}

			if (PropertyHandle.IsValid())
			{
				// No property editor was specified so check if multiple property values are associated with the property handle
				TArray<FString> ObjectValues;
				PropertyHandle->GetPerObjectValues(ObjectValues);

				if (ObjectValues.Num() > 1)
				{
					for (int32 ObjectIndex = 1; ObjectIndex < ObjectValues.Num() && Result == FPropertyAccess::Success; ++ObjectIndex)
					{
						if (ObjectValues[ObjectIndex] != ObjectValues[0])
						{
							Result = FPropertyAccess::MultipleValues;
						}
					}
				}
			}
		}
	}

	return Result;
}

UClass* SPropertyEditorAsset::GetDisplayedClass() const
{
	FObjectOrAssetData Value;
	GetValue( Value );
	if(Value.Object != nullptr)
	{
		return Value.Object->GetClass();
	}
	else
	{
		return ObjectClass;	
	}
}

void SPropertyEditorAsset::OnAssetSelected( const struct FAssetData& AssetData )
{
	SetValue(AssetData);
}

void SPropertyEditorAsset::OnActorSelected( AActor* InActor )
{
	SetValue(InActor);
}

void SPropertyEditorAsset::OnGetAllowedClasses(TArray<const UClass*>& AllowedClasses)
{
	AllowedClasses.Append(AllowedClassFilters);
}

void SPropertyEditorAsset::OnOpenAssetEditor()
{
	FObjectOrAssetData Value;
	GetValue( Value );

	UObject* ObjectToEdit = Value.AssetData.GetAsset();
	if( ObjectToEdit )
	{
		GEditor->EditObject( ObjectToEdit );
	}
}

void SPropertyEditorAsset::OnBrowse()
{
	FObjectOrAssetData Value;
	GetValue( Value );

	if(PropertyEditor.IsValid() && Value.Object)
	{
		// This code only works on loaded objects
		FPropertyEditor::SyncToObjectsInNode(PropertyEditor->GetPropertyNode());		
	}
	else
	{
		TArray<FAssetData> AssetDataList;
		AssetDataList.Add( Value.AssetData );
		GEditor->SyncBrowserToObjects( AssetDataList );
	}
}

FText SPropertyEditorAsset::GetOnBrowseToolTip() const
{
	FObjectOrAssetData Value;
	GetValue( Value );

	if (Value.Object)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Asset"), FText::AsCultureInvariant(Value.Object->GetName()));
		if (bIsActor)
		{
			return FText::Format(LOCTEXT( "BrowseToAssetInViewport", "Select '{Asset}' in the viewport"), Args);
		}
		else
		{
			return FText::Format(LOCTEXT( "BrowseToSpecificAssetInContentBrowser", "Browse to '{Asset}' in Content Browser"), Args);
		}
	}
	
	return LOCTEXT( "BrowseToAssetInContentBrowser", "Browse to Asset in Content Browser");
}

void SPropertyEditorAsset::OnUse()
{
	// Use the property editor path if it is valid and there is no custom filtering required
	if(PropertyEditor.IsValid()
		&& !OnShouldFilterAsset.IsBound()
		&& AllowedClassFilters.Num() == 0
		&& DisallowedClassFilters.Num() == 0
		&& (GEditor ? !GEditor->MakeAssetReferenceFilter(FAssetReferenceFilterContext()).IsValid() : true))
	{
		PropertyEditor->GetPropertyHandle()->SetObjectValueFromSelection();
	}
	else
	{
		// Load selected assets
		FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();

		// try to get a selected object of our class
		UObject* Selection = nullptr;
		if( ObjectClass && ObjectClass->IsChildOf( AActor::StaticClass() ) )
		{
			Selection = GEditor->GetSelectedActors()->GetTop( ObjectClass );
		}
		else if( ObjectClass )
		{
			// Get the first material selected
			Selection = GEditor->GetSelectedObjects()->GetTop( ObjectClass );
		}

		// Check against custom asset filter
		if (Selection != nullptr
			&& OnShouldFilterAsset.IsBound()
			&& OnShouldFilterAsset.Execute(FAssetData(Selection)))
		{
			Selection = nullptr;
		}

		if( Selection )
		{
			SetValue( Selection );
		}
	}
}

void SPropertyEditorAsset::OnClear()
{
	SetValue(nullptr);
}

FSlateColor SPropertyEditorAsset::GetAssetClassColor()
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));	
	TWeakPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(GetDisplayedClass());
	if(AssetTypeActions.IsValid())
	{
		return FSlateColor(AssetTypeActions.Pin()->GetTypeColor());
	}

	return FSlateColor::UseForeground();
}

bool SPropertyEditorAsset::OnAssetDraggedOver( const UObject* InObject ) const
{
	if (CanEdit() && InObject != nullptr && InObject->IsA(ObjectClass))
	{
		// Check against custom asset filter
		FAssetData AssetData(InObject);
		if (!OnShouldFilterAsset.IsBound()
			|| !OnShouldFilterAsset.Execute(AssetData))
		{
			if (CanSetBasedOnCustomClasses(AssetData))
			{
				return CanSetBasedOnAssetReferenceFilter(AssetData);
			}
		}
	}

	return false;
}

void SPropertyEditorAsset::OnAssetDropped( UObject* InObject )
{
	if( CanEdit() )
	{
		SetValue(InObject);
	}
}


void SPropertyEditorAsset::OnCopy()
{
	FObjectOrAssetData Value;
	GetValue( Value );

	if( Value.AssetData.IsValid() )
	{
		FPlatformApplicationMisc::ClipboardCopy(*Value.AssetData.GetExportTextName());
	}
	else
	{
		FPlatformApplicationMisc::ClipboardCopy(*Value.ObjectPath.ToString());
	}
}

void SPropertyEditorAsset::OnPaste()
{
	FString DestPath;
	FPlatformApplicationMisc::ClipboardPaste(DestPath);

	if(DestPath == TEXT("None"))
	{
		SetValue(nullptr);
	}
	else
	{
		UObject* Object = LoadObject<UObject>(nullptr, *DestPath);
		if(Object && Object->IsA(ObjectClass))
		{
			// Check against custom asset filter
			if (!OnShouldFilterAsset.IsBound()
				|| !OnShouldFilterAsset.Execute(FAssetData(Object)))
			{
				SetValue(Object);
			}
		}
	}
}

bool SPropertyEditorAsset::CanPaste()
{
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

	const FString PossibleObjectPath = FPackageName::ExportTextPathToObjectPath(ClipboardText);

	bool bCanPaste = false;

	if( CanEdit() )
	{
		if( PossibleObjectPath == TEXT("None") )
		{
			bCanPaste = true;
		}
		else
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			bCanPaste = PossibleObjectPath.Len() < NAME_SIZE && AssetRegistryModule.Get().GetAssetByObjectPath( *PossibleObjectPath ).IsValid();
		}
	}

	return bCanPaste;
}

FReply SPropertyEditorAsset::OnAssetThumbnailDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent )
{
	OnOpenAssetEditor();
	return FReply::Handled();
}

bool SPropertyEditorAsset::CanEdit() const
{
	return PropertyEditor.IsValid() ? !PropertyEditor->IsEditConst() : true;
}

bool SPropertyEditorAsset::CanSetBasedOnCustomClasses( const FAssetData& InAssetData ) const
{
	if (InAssetData.IsValid())
	{
		return IsClassAllowed(InAssetData.GetClass());
	}

	return true;
}

bool SPropertyEditorAsset::IsClassAllowed(const UClass* InClass) const
{
	bool bClassAllowed = true;
	if (AllowedClassFilters.Num() > 0)
	{
		bClassAllowed = false;
		for (const UClass* AllowedClass : AllowedClassFilters)
		{
			const bool bAllowedClassIsInterface = AllowedClass->HasAnyClassFlags(CLASS_Interface);
			bClassAllowed = bExactClass ? InClass == AllowedClass :
				InClass->IsChildOf(AllowedClass) || (bAllowedClassIsInterface && InClass->ImplementsInterface(AllowedClass));

			if (bClassAllowed)
			{
				break;
			}
		}
	}

	if (DisallowedClassFilters.Num() > 0 && bClassAllowed)
	{
		for (const UClass* DisallowedClass : DisallowedClassFilters)
		{
			const bool bDisallowedClassIsInterface = DisallowedClass->HasAnyClassFlags(CLASS_Interface);
			if (InClass->IsChildOf(DisallowedClass) || (bDisallowedClassIsInterface && InClass->ImplementsInterface(DisallowedClass)))
			{
				bClassAllowed = false;
				break;
			}
		}
	}

	return bClassAllowed;
}

bool SPropertyEditorAsset::CanSetBasedOnAssetReferenceFilter( const FAssetData& InAssetData, FText* OutOptionalFailureReason) const
{
	if (GEditor && InAssetData.IsValid())
	{
		TSharedPtr<IPropertyHandle> PropertyHandleToUse = GetMostSpecificPropertyHandle();
		FAssetReferenceFilterContext AssetReferenceFilterContext;
		if (PropertyHandleToUse.IsValid())
		{
			TArray<UObject*> ReferencingObjects;
			PropertyHandleToUse->GetOuterObjects(ReferencingObjects);
			for (UObject* ReferencingObject : ReferencingObjects)
			{
				AssetReferenceFilterContext.ReferencingAssets.Add(FAssetData(ReferencingObject));
			}
		}
		
		if(OwnerAssetDataArray.Num() > 0)
		{
			for (const FAssetData& AssetData : OwnerAssetDataArray)
			{
				if (AssetData.IsValid())
				{
					//Use add unique in case the PropertyHandle as already add the referencing asset
					AssetReferenceFilterContext.ReferencingAssets.AddUnique(AssetData);
				}
			}
		}

		TSharedPtr<IAssetReferenceFilter> AssetReferenceFilter = GEditor->MakeAssetReferenceFilter(AssetReferenceFilterContext);
		if (AssetReferenceFilter.IsValid() && !AssetReferenceFilter->PassesFilter(InAssetData, OutOptionalFailureReason))
		{
			return false;
		}
	}

	return true;
}

TSharedPtr<IPropertyHandle> SPropertyEditorAsset::GetMostSpecificPropertyHandle() const
{
	if (PropertyHandle.IsValid())
	{
		return PropertyHandle;
	}
	else if (PropertyEditor.IsValid())
	{
		return PropertyEditor->GetPropertyHandle();
	}
	
	return TSharedPtr<IPropertyHandle>();
}

UClass* SPropertyEditorAsset::GetObjectPropertyClass(const FProperty* Property)
{
	UClass* Class = nullptr;

	if (CastField<const FObjectPropertyBase>(Property) != nullptr)
	{
		Class = CastField<const FObjectPropertyBase>(Property)->PropertyClass;
	}
	else if (CastField<const FInterfaceProperty>(Property) != nullptr)
	{
		Class = CastField<const FInterfaceProperty>(Property)->InterfaceClass;
	}

	if (!ensureMsgf(Class != nullptr, TEXT("Property (%s) is not an object or interface class"), Property ? *Property->GetFullName() : TEXT("null")))
	{
		Class = UObject::StaticClass();
	}
	return Class;
}

#undef LOCTEXT_NAMESPACE
