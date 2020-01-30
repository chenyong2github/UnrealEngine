// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/RichTextBlock.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Font.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Components/RichTextBlockDecorator.h"
#include "Styling/SlateStyle.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/RichTextMarkupProcessing.h"
#include "Framework/Text/IRichTextMarkupParser.h"
#include "Framework/Text/IRichTextMarkupWriter.h"
#include "RenderingThread.h"
#include "Editor/WidgetCompilerLog.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// URichTextBlock

template< class ObjectType >
struct FDeferredDeletor : public FDeferredCleanupInterface
{
public:
	FDeferredDeletor(ObjectType* InInnerObjectToDelete)
		: InnerObjectToDelete(InInnerObjectToDelete)
	{
	}

	virtual ~FDeferredDeletor()
	{
		delete InnerObjectToDelete;
	}

private:
	ObjectType* InnerObjectToDelete;
};

template< class ObjectType >
FORCEINLINE SharedPointerInternals::FRawPtrProxy< ObjectType > MakeShareableDeferredCleanup(ObjectType* InObject)
{
	return MakeShareable(InObject, [](ObjectType* ObjectToDelete) { BeginCleanup(new FDeferredDeletor<ObjectType>(ObjectToDelete)); });
}

URichTextBlock::URichTextBlock(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Visibility = ESlateVisibility::SelfHitTestInvisible;
}

void URichTextBlock::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyRichTextBlock.Reset();
	StyleInstance.Reset();
}

TSharedRef<SWidget> URichTextBlock::RebuildWidget()
{
	UpdateStyleData();

	TArray< TSharedRef< class ITextDecorator > > CreatedDecorators;
	CreateDecorators(CreatedDecorators);

	TSharedRef<FRichTextLayoutMarshaller> Marshaller = FRichTextLayoutMarshaller::Create(CreateMarkupParser(), CreateMarkupWriter(), CreatedDecorators, StyleInstance.Get());

	MyRichTextBlock =
		SNew(SRichTextBlock)
		.TextStyle(bOverrideDefaultStyle ? &DefaultTextStyleOverride : &DefaultTextStyle)
		.Marshaller(Marshaller);
	
	return MyRichTextBlock.ToSharedRef();
}

void URichTextBlock::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	MyRichTextBlock->SetText(Text);

	Super::SynchronizeTextLayoutProperties( *MyRichTextBlock );
}

void URichTextBlock::UpdateStyleData()
{
	if (IsDesignTime())
	{
		InstanceDecorators.Reset();
	}

	if (!StyleInstance.IsValid())
	{
		RebuildStyleInstance();

		for (TSubclassOf<URichTextBlockDecorator> DecoratorClass : DecoratorClasses)
		{
			if (UClass* ResolvedClass = DecoratorClass.Get())
			{
				if (!ResolvedClass->HasAnyClassFlags(CLASS_Abstract))
				{
					URichTextBlockDecorator* Decorator = NewObject<URichTextBlockDecorator>(this, ResolvedClass);
					InstanceDecorators.Add(Decorator);
				}
			}
		}
	}
}

FText URichTextBlock::GetText() const
{
	if (MyRichTextBlock.IsValid())
	{
		return MyRichTextBlock->GetText();
	}

	return Text;
}

void URichTextBlock::SetText(const FText& InText)
{
	Text = InText;
	if (MyRichTextBlock.IsValid())
	{
		MyRichTextBlock->SetText(InText);
	}
}

void URichTextBlock::RebuildStyleInstance()
{
	StyleInstance = MakeShareableDeferredCleanup(new FSlateStyleSet(TEXT("RichTextStyle")));

	if (TextStyleSet && TextStyleSet->GetRowStruct()->IsChildOf(FRichTextStyleRow::StaticStruct()))
	{
		for (const auto& Entry : TextStyleSet->GetRowMap())
		{
			FName SubStyleName = Entry.Key;
			FRichTextStyleRow* RichTextStyle = (FRichTextStyleRow*)Entry.Value;

			if (SubStyleName == FName(TEXT("Default")))
			{
				DefaultTextStyle = RichTextStyle->TextStyle;
			}

			StyleInstance->Set(SubStyleName, RichTextStyle->TextStyle);
		}
	}
}

void URichTextBlock::SetTextStyleSet(class UDataTable* NewTextStyleSet)
{
	if (TextStyleSet != NewTextStyleSet)
	{
		TextStyleSet = NewTextStyleSet;

		RebuildStyleInstance();

		MyRichTextBlock->SetDecoratorStyleSet(StyleInstance.Get());
		MyRichTextBlock->SetTextStyle(DefaultTextStyle);
	}
}

const FTextBlockStyle& URichTextBlock::GetDefaultTextStyle() const
{
	ensure(StyleInstance.IsValid());
	return DefaultTextStyle;
}

const FTextBlockStyle& URichTextBlock::GetCurrentDefaultTextStyle() const
{
	if (bOverrideDefaultStyle)
	{
		return DefaultTextStyleOverride;
	}
	else
	{
		ensure(StyleInstance.IsValid());
		return DefaultTextStyle;
	}
}

URichTextBlockDecorator* URichTextBlock::GetDecoratorByClass(TSubclassOf<URichTextBlockDecorator> DecoratorClass)
{
	for (URichTextBlockDecorator* Decorator : InstanceDecorators)
	{
		if (Decorator->IsA(DecoratorClass))
		{
			return Decorator;
		}
	}

	return nullptr;
}

void URichTextBlock::CreateDecorators(TArray< TSharedRef< class ITextDecorator > >& OutDecorators)
{
	for (URichTextBlockDecorator* Decorator : InstanceDecorators)
	{
		if (Decorator)
		{
			TSharedPtr<ITextDecorator> TextDecorator = Decorator->CreateDecorator(this);
			if (TextDecorator.IsValid())
			{
				OutDecorators.Add(TextDecorator.ToSharedRef());
			}
		}
	}
}

TSharedPtr< IRichTextMarkupParser > URichTextBlock::CreateMarkupParser()
{
	return FDefaultRichTextMarkupParser::GetStaticInstance();
}

TSharedPtr< IRichTextMarkupWriter > URichTextBlock::CreateMarkupWriter()
{
	return FDefaultRichTextMarkupWriter::Create();
}

void URichTextBlock::BeginDefaultStyleOverride()
{
	if (!bOverrideDefaultStyle)
	{
		// If we aren't already overriding, make sure override style starts off matching the existing default
		bOverrideDefaultStyle = true;
		DefaultTextStyleOverride = DefaultTextStyle;
	}
}

#if WITH_EDITOR

const FText URichTextBlock::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}

void URichTextBlock::OnCreationFromPalette()
{
	//Decorators.Add(NewObject<URichTextBlockDecorator>(this, NAME_None, RF_Transactional));
}

void URichTextBlock::ValidateCompiledDefaults(IWidgetCompilerLog& CompileLog) const
{
	Super::ValidateCompiledDefaults(CompileLog);

	if (TextStyleSet && !TextStyleSet->GetRowStruct()->IsChildOf(FRichTextStyleRow::StaticStruct()))
	{
		CompileLog.Warning(FText::Format(
			LOCTEXT("RichTextBlock_InvalidTextStyle", "{0} Text Style Set property expects a Data Table with a Rich Text Style Row structure (currently set to {1})."), 
			FText::FromString(GetName()), 
			FText::AsCultureInvariant(TextStyleSet->GetPathName())));
	}
}

#endif //if WITH_EDITOR

void URichTextBlock::SetDefaultTextStyle(const FTextBlockStyle& InDefaultTextStyle)
{
	BeginDefaultStyleOverride();
	DefaultTextStyleOverride = InDefaultTextStyle;
	ApplyUpdatedDefaultTextStyle();
}

void URichTextBlock::ClearAllDefaultStyleOverrides()
{
	if (bOverrideDefaultStyle)
	{
		bOverrideDefaultStyle = false;
		ApplyUpdatedDefaultTextStyle();
	}
}

void URichTextBlock::SetDefaultColorAndOpacity(FSlateColor InColorAndOpacity)
{
	BeginDefaultStyleOverride();
	DefaultTextStyleOverride.ColorAndOpacity = InColorAndOpacity;
	ApplyUpdatedDefaultTextStyle();
}


void URichTextBlock::SetDefaultShadowColorAndOpacity(FLinearColor InShadowColorAndOpacity)
{
	BeginDefaultStyleOverride();
	DefaultTextStyleOverride.ShadowColorAndOpacity = InShadowColorAndOpacity;
	ApplyUpdatedDefaultTextStyle();
}

void URichTextBlock::SetDefaultShadowOffset(FVector2D InShadowOffset)
{
	BeginDefaultStyleOverride();
	DefaultTextStyleOverride.ShadowOffset = InShadowOffset;
	ApplyUpdatedDefaultTextStyle();
}

void URichTextBlock::SetDefaultFont(FSlateFontInfo InFontInfo)
{
	BeginDefaultStyleOverride();
	DefaultTextStyleOverride.Font = InFontInfo;
	ApplyUpdatedDefaultTextStyle();
}

void URichTextBlock::SetDefaultStrikeBrush(FSlateBrush& InStrikeBrush)
{
	BeginDefaultStyleOverride();
	DefaultTextStyleOverride.StrikeBrush = InStrikeBrush;
	ApplyUpdatedDefaultTextStyle();
}

void URichTextBlock::SetJustification(ETextJustify::Type InJustification)
{
	Super::SetJustification(InJustification);
	if (MyRichTextBlock.IsValid())
	{
		MyRichTextBlock->SetJustification(InJustification);
	}
}

void URichTextBlock::SetMinDesiredWidth(float InMinDesiredWidth)
{
	MinDesiredWidth = InMinDesiredWidth;
	if (MyRichTextBlock.IsValid())
	{
		MyRichTextBlock->SetMinDesiredWidth(InMinDesiredWidth);
	}
}

void URichTextBlock::SetAutoWrapText(bool InAutoTextWrap)
{
	AutoWrapText = InAutoTextWrap;
	if (MyRichTextBlock.IsValid())
	{
		MyRichTextBlock->SetAutoWrapText(InAutoTextWrap);
	}
}

void URichTextBlock::ApplyUpdatedDefaultTextStyle()
{
	if (MyRichTextBlock.IsValid())
	{
		MyRichTextBlock->SetTextStyle(bOverrideDefaultStyle ? DefaultTextStyleOverride : DefaultTextStyle);
	}
}

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
