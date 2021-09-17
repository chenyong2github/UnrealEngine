// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "MovieScene.h"
#include "Animation/WidgetAnimation.h"
#include "Serialization/TextReferenceCollector.h"
#include "Engine/UserInterfaceSettings.h"
#include "UMGPrivate.h"
#include "Interfaces/ITargetPlatform.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/ObjectResource.h"
#include "UObject/LinkerLoad.h"
#include "Engine/StreamableManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Engine/Blueprint.h"
#endif

#define LOCTEXT_NAMESPACE "UMG"

FAutoConsoleCommand GDumpTemplateSizesCommand(
	TEXT("Widget.DumpTemplateSizes"),
	TEXT("Dump the sizes of all widget class templates in memory"),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		struct FClassAndSize
		{
			FString ClassName;
			int32 TemplateSize = 0;
		};

		TArray<FClassAndSize> TemplateSizes;

		for (TObjectIterator<UWidgetBlueprintGeneratedClass> WidgetClassIt; WidgetClassIt; ++WidgetClassIt)
		{
			UWidgetBlueprintGeneratedClass* WidgetClass = *WidgetClassIt;

			if (WidgetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}

#if WITH_EDITOR
			if (Cast<UBlueprint>(WidgetClass->ClassGeneratedBy)->SkeletonGeneratedClass == WidgetClass)
			{
				continue;
			}
#endif

			FClassAndSize Entry;
			Entry.ClassName = WidgetClass->GetName();

			if (UUserWidget* TemplateWidget = WidgetClass->GetDefaultObject<UUserWidget>())
			{
				int32 TemplateSize = WidgetClass->GetStructureSize();
				if (const UWidgetTree* TemplateWidgetTree = WidgetClass->GetWidgetTreeArchetype())
				{
					TemplateWidgetTree->ForEachWidget([&TemplateSize](UWidget* Widget) {
						TemplateSize += Widget->GetClass()->GetStructureSize();
					});
				}

				Entry.TemplateSize = TemplateSize;
			}

			TemplateSizes.Add(Entry);
		}

		TemplateSizes.StableSort([](const FClassAndSize& A, const FClassAndSize& B) {
			return A.TemplateSize > B.TemplateSize;
		});

		uint32 TotalSizeBytes = 0;
		UE_LOG(LogUMG, Display, TEXT("%-60s %-15s"), TEXT("Template Class"), TEXT("Size (bytes)"));
		for (const FClassAndSize& Entry : TemplateSizes)
		{
			TotalSizeBytes += Entry.TemplateSize;
			if (Entry.TemplateSize > 0)
			{
				UE_LOG(LogUMG, Display, TEXT("%-60s %-15d"), *Entry.ClassName, Entry.TemplateSize);
			}
			else
			{
				UE_LOG(LogUMG, Display, TEXT("%-60s %-15s"), *Entry.ClassName, TEXT("0 - (No Template)"));
			}
		}

		UE_LOG(LogUMG, Display, TEXT("Total size of templates %.3f MB"), TotalSizeBytes/(1024.f*1024.f));
	}), ECVF_Cheat);

#if WITH_EDITOR

int32 TemplatePreviewInEditor = 0;
static FAutoConsoleVariableRef CVarTemplatePreviewInEditor(TEXT("Widget.TemplatePreviewInEditor"), TemplatePreviewInEditor, TEXT("Should a dynamic template be generated at runtime for the editor for widgets?  Useful for debugging templates."), ECVF_Default);

#endif

#if WITH_EDITORONLY_DATA
namespace
{
	void CollectWidgetBlueprintGeneratedClassTextReferences(UObject* Object, FArchive& Ar)
	{
		// In an editor build, both UWidgetBlueprint and UWidgetBlueprintGeneratedClass reference an identical WidgetTree.
		// So we ignore the UWidgetBlueprintGeneratedClass when looking for persistent text references since it will be overwritten by the UWidgetBlueprint version.
	}
}
#endif

/////////////////////////////////////////////////////
// UWidgetBlueprintGeneratedClass

UWidgetBlueprintGeneratedClass::UWidgetBlueprintGeneratedClass(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	{ static const FAutoRegisterTextReferenceCollectorCallback AutomaticRegistrationOfTextReferenceCollector(UWidgetBlueprintGeneratedClass::StaticClass(), &CollectWidgetBlueprintGeneratedClassTextReferences); }
	bCanCallPreConstruct = true;
#endif
}

void UWidgetBlueprintGeneratedClass::InitializeBindingsStatic(UUserWidget* UserWidget, const TArray< FDelegateRuntimeBinding >& InBindings)
{
	check(!UserWidget->IsTemplate());

	// Note: It's not safe to assume here that the UserWidget class type is a UWidgetBlueprintGeneratedClass!
	// - @see InitializeWidgetStatic()

	// For each property binding that we're given, find the corresponding field, and setup the delegate binding on the widget.
	for (const FDelegateRuntimeBinding& Binding : InBindings)
	{
		// If the binding came from a parent class, this will still find it - FindField() searches the super class hierarchy by default.
		FObjectProperty* WidgetProperty = FindFProperty<FObjectProperty>(UserWidget->GetClass(), *Binding.ObjectName);
		if (WidgetProperty == nullptr)
		{
			continue;
		}

		UWidget* Widget = Cast<UWidget>(WidgetProperty->GetObjectPropertyValue_InContainer(UserWidget));

		if (Widget)
		{
			FDelegateProperty* DelegateProperty = FindFProperty<FDelegateProperty>(Widget->GetClass(), FName(*(Binding.PropertyName.ToString() + TEXT("Delegate"))));
			if (!DelegateProperty)
			{
				DelegateProperty = FindFProperty<FDelegateProperty>(Widget->GetClass(), Binding.PropertyName);
			}

			if (DelegateProperty)
			{
				bool bSourcePathBound = false;

				if (Binding.SourcePath.IsValid())
				{
					bSourcePathBound = Widget->AddBinding(DelegateProperty, UserWidget, Binding.SourcePath);
				}

				// If no native binder is found then the only possibility is that the binding is for
				// a delegate that doesn't match the known native binders available and so we
				// fallback to just attempting to bind to the function directly.
				if (bSourcePathBound == false)
				{
					FScriptDelegate* ScriptDelegate = DelegateProperty->GetPropertyValuePtr_InContainer(Widget);
					if (ScriptDelegate)
					{
						ScriptDelegate->BindUFunction(UserWidget, Binding.FunctionName);
					}
				}
			}
		}
	}
}

void UWidgetBlueprintGeneratedClass::InitializeWidgetStatic(UUserWidget* UserWidget
	, const UClass* InClass
	, UWidgetTree* InWidgetTree
	, const TArray< UWidgetAnimation* >& InAnimations
	, const TArray< FDelegateRuntimeBinding >& InBindings)
{
	check(InClass);

	// Note: It's not safe to assume here that the UserWidget class type is a UWidgetBlueprintGeneratedClass! In the case of a nativized widget
	// blueprint class, it will be a UDynamicClass instead, and this API will be invoked by the blueprint's C++ code that's generated at cook time.
	// - @see FBackendHelperUMG::EmitWidgetInitializationFunctions()

	if ( UserWidget->IsTemplate() )
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<UClass> WidgetGeneratedByClass = MakeWeakObjectPtr(const_cast<UClass*>(InClass));
	UserWidget->WidgetGeneratedByClass = WidgetGeneratedByClass;
#endif

	UWidgetTree* ClonedTree = UserWidget->WidgetTree;

	// Normally the ClonedTree should be null - we do in the case of design time with the widget, actually
	// clone the widget tree directly from the WidgetBlueprint so that the rebuilt preview matches the newest
	// widget tree, without a full blueprint compile being required.  In that case, the WidgetTree on the UserWidget
	// will have already been initialized to some value.  When that's the case, we'll avoid duplicating it from the class
	// similar to how we use to use the DesignerWidgetTree.
	if ( ClonedTree == nullptr )
	{
		UserWidget->DuplicateAndInitializeFromWidgetTree(InWidgetTree);
		ClonedTree = UserWidget->WidgetTree;
	}

#if !WITH_EDITOR && UE_BUILD_DEBUG
	UE_LOG(LogUMG, Warning, TEXT("Widget Class %s - Slow Static Duplicate Object."), *InClass->GetName());
#endif

#if WITH_EDITOR
	UserWidget->WidgetGeneratedBy = InClass->ClassGeneratedBy;
#endif

	if (ClonedTree)
	{
		BindAnimations(UserWidget, InAnimations);

		UClass* WidgetBlueprintClass = UserWidget->GetClass();

		ClonedTree->ForEachWidget([&](UWidget* Widget) {
			// Not fatal if NULL, but shouldn't happen
			if (!ensure(Widget != nullptr))
			{
				return;
			}

#if !UE_BUILD_SHIPPING
			Widget->WidgetGeneratedByClass = WidgetGeneratedByClass;
#endif

#if WITH_EDITOR
			Widget->WidgetGeneratedBy = InClass->ClassGeneratedBy;
#endif

			// TODO UMG Make this an FName
			FString VariableName = Widget->GetName();

			// Find property with the same name as the template and assign the new widget to it.
			FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(WidgetBlueprintClass, *VariableName);
			if (Prop)
			{
				Prop->SetObjectPropertyValue_InContainer(UserWidget, Widget);
				UObject* Value = Prop->GetObjectPropertyValue_InContainer(UserWidget);
				check(Value == Widget);
			}

			// Initialize Navigation Data
			if (Widget->Navigation)
			{
				Widget->Navigation->ResolveRules(UserWidget, ClonedTree);
			}

#if WITH_EDITOR
			Widget->ConnectEditorData();
#endif
		});

		InitializeBindingsStatic(UserWidget, InBindings);

		// Bind any delegates on widgets
		UBlueprintGeneratedClass::BindDynamicDelegates(InClass, UserWidget);

		//TODO UMG Add OnWidgetInitialized?
	}
}

void UWidgetBlueprintGeneratedClass::BindAnimations(UUserWidget* Instance, const TArray< UWidgetAnimation* >& InAnimations)
{
	// Note: It's not safe to assume here that the UserWidget class type is a UWidgetBlueprintGeneratedClass!
	// - @see InitializeWidgetStatic()

	for (UWidgetAnimation* Animation : InAnimations)
	{
		if (Animation->GetMovieScene())
		{
			// Find property with the same name as the animation and assign the animation to it.
			FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(Instance->GetClass(), Animation->GetMovieScene()->GetFName());
			if (Prop)
			{
				Prop->SetObjectPropertyValue_InContainer(Instance, Animation);
			}
		}
	}
}

#if WITH_EDITOR
void UWidgetBlueprintGeneratedClass::SetClassRequiresNativeTick(bool InClassRequiresNativeTick)
{
	bClassRequiresNativeTick = InClassRequiresNativeTick;
}
#endif

void UWidgetBlueprintGeneratedClass::InitializeWidget(UUserWidget* UserWidget) const
{
	TArray<UWidgetAnimation*> AllAnims;
	TArray<FDelegateRuntimeBinding> AllBindings;

	// Include current class animations.
	AllAnims.Append(Animations);

	// Include current class bindings.
	AllBindings.Append(Bindings);

	// Iterate all generated classes in the widget's parent class hierarchy and include animations and bindings found on each one.
	UClass* SuperClass = GetSuperClass();
	while (UWidgetBlueprintGeneratedClass* WBPGC = Cast<UWidgetBlueprintGeneratedClass>(SuperClass))
	{
		AllAnims.Append(WBPGC->Animations);
		AllBindings.Append(WBPGC->Bindings);

		SuperClass = SuperClass->GetSuperClass();
	}

	InitializeWidgetStatic(UserWidget, this, WidgetTree, AllAnims, AllBindings);
}

void UWidgetBlueprintGeneratedClass::PostLoad()
{
	Super::PostLoad();

	if (WidgetTree)
	{
		// We don't want any of these flags to carry over from the WidgetBlueprint
		WidgetTree->ClearFlags(RF_Public | RF_ArchetypeObject | RF_DefaultSubObject);

#if !WITH_EDITOR
		WidgetTree->AddToCluster(this, true);
#endif
	}

#if WITH_EDITOR
	if ( GetLinkerUE4Version() < VER_UE4_RENAME_WIDGET_VISIBILITY )
	{
		static const FName Visiblity(TEXT("Visiblity"));
		static const FName Visibility(TEXT("Visibility"));

		for ( FDelegateRuntimeBinding& Binding : Bindings )
		{
			if ( Binding.PropertyName == Visiblity )
			{
				Binding.PropertyName = Visibility;
			}
		}
	}
#endif
}

void UWidgetBlueprintGeneratedClass::PurgeClass(bool bRecompilingOnLoad)
{
	Super::PurgeClass(bRecompilingOnLoad);

	const ERenameFlags RenFlags = REN_DontCreateRedirectors | ( ( bRecompilingOnLoad ) ? REN_ForceNoResetLoaders : 0 ) | REN_NonTransactional | REN_DoNotDirty;

	// Remove the old widdget tree.
	if ( WidgetTree )
	{
		WidgetTree->Rename(nullptr, GetTransientPackage(), RenFlags);
		FLinkerLoad::InvalidateExport(WidgetTree);
		WidgetTree = nullptr;
	}

	// Remove all animations.
	for ( UWidgetAnimation* Animation : Animations )
	{
		Animation->Rename(nullptr, GetTransientPackage(), RenFlags);
		FLinkerLoad::InvalidateExport(Animation);
	}
	Animations.Empty();

	Bindings.Empty();
}

bool UWidgetBlueprintGeneratedClass::NeedsLoadForServer() const
{
	const UUserInterfaceSettings* UISettings = GetDefault<UUserInterfaceSettings>(UUserInterfaceSettings::StaticClass());
	check(UISettings);
	return UISettings->bLoadWidgetsOnDedicatedServer;
}

void UWidgetBlueprintGeneratedClass::SetWidgetTreeArchetype(UWidgetTree* InWidgetTree)
{
	WidgetTree = InWidgetTree;

	if (WidgetTree)
	{
		// We don't want any of these flags to carry over from the WidgetBlueprint
		WidgetTree->ClearFlags(RF_Public | RF_ArchetypeObject | RF_DefaultSubObject);
	}
}

void UWidgetBlueprintGeneratedClass::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
}

UWidgetBlueprintGeneratedClass* UWidgetBlueprintGeneratedClass::FindWidgetTreeOwningClass()
{
	UWidgetBlueprintGeneratedClass* RootBGClass = this;
	UWidgetBlueprintGeneratedClass* BGClass = RootBGClass;

	while (BGClass)
	{
		//TODO NickD: This conditional post load shouldn't be needed any more once the Fast Widget creation path is the only path!
		// Force post load on the generated class so all subobjects are done (specifically the widget tree).
		BGClass->ConditionalPostLoad();

		const bool bNoRootWidget = (nullptr == BGClass->WidgetTree) || (nullptr == BGClass->WidgetTree->RootWidget);

		if (bNoRootWidget)
		{
			UWidgetBlueprintGeneratedClass* SuperBGClass = Cast<UWidgetBlueprintGeneratedClass>(BGClass->GetSuperClass());
			if (SuperBGClass)
			{
				BGClass = SuperBGClass;
				continue;
			}
			else
			{
				// If we reach a super class that isn't a UWidgetBlueprintGeneratedClass, return the root class.
				return RootBGClass;
			}
		}

		return BGClass;
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
