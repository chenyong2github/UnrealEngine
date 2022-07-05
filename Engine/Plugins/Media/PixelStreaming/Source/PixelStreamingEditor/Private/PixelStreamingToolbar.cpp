// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingToolbar.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "PixelStreamingCommands.h"
#include "PixelStreamingStyle.h"
#include "Framework/SlateDelegates.h"
#include "ToolMenuContext.h"
#include "IPixelStreamingModule.h"
#include "IPixelStreamingStreamer.h"
#include "Editor/EditorEngine.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SWindow.h"
#include "EditorViewportClient.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"
#include "Widgets/Layout/SSpacer.h"
#include "Styling/AppStyle.h"
#include "PixelStreamingCodec.h"
#include "PixelStreamingEditorModule.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Types/SlateEnums.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "PixelStreamingEditor"

namespace UE::PixelStreaming
{
    FPixelStreamingToolbar::FPixelStreamingToolbar()
    : PixelStreamingModule(IPixelStreamingModule::Get())
    {
        FPixelStreamingCommands::Register();

        PluginCommands = MakeShared<FUICommandList>();
        
        PluginCommands->MapAction(
            FPixelStreamingCommands::Get().StartStreaming,
            FExecuteAction::CreateLambda([]()
            {
                FPixelStreamingEditorModule::GetModule()->StartStreaming();
            }),
            FCanExecuteAction::CreateLambda([Module = &PixelStreamingModule] {  
                if(TSharedPtr<IPixelStreamingStreamer> Streamer = Module->GetStreamer(Module->GetDefaultStreamerID()))    
                {
                    return !Streamer->IsStreaming();
                }                 
                return false;
            })
        );
       
        PluginCommands->MapAction(
            FPixelStreamingCommands::Get().StopStreaming,
            FExecuteAction::CreateLambda([]()
            {
                FPixelStreamingEditorModule::GetModule()->StopStreaming();
            }),
            FCanExecuteAction::CreateLambda([Module = &PixelStreamingModule] { 
                if(TSharedPtr<IPixelStreamingStreamer> Streamer = Module->GetStreamer(Module->GetDefaultStreamerID()))    
                {
                    return Streamer->IsStreaming();
                }
                return true;
            })
        );

        PluginCommands->MapAction(
            FPixelStreamingCommands::Get().VP8,
            FExecuteAction::CreateLambda([Module = &PixelStreamingModule]()
            {
                Module->SetCodec(EPixelStreamingCodec::VP8);
            }),
            FCanExecuteAction(),
            FIsActionChecked::CreateLambda([Module = &PixelStreamingModule]()
            { 
                return Module->GetCodec() == EPixelStreamingCodec::VP8;
            })
        );

        PluginCommands->MapAction(
            FPixelStreamingCommands::Get().VP9,
            FExecuteAction::CreateLambda([Module = &PixelStreamingModule]()
            {
                Module->SetCodec(EPixelStreamingCodec::VP9);
            }),
            FCanExecuteAction(),
            FIsActionChecked::CreateLambda([Module = &PixelStreamingModule]()
            { 
                return Module->GetCodec() == EPixelStreamingCodec::VP9;
            })
        );

        PluginCommands->MapAction(
            FPixelStreamingCommands::Get().H264,
            FExecuteAction::CreateLambda([Module = &PixelStreamingModule]()
            {
                Module->SetCodec(EPixelStreamingCodec::H264);
            }),
            FCanExecuteAction(),
            FIsActionChecked::CreateLambda([Module = &PixelStreamingModule]()
            { 
                return Module->GetCodec() == EPixelStreamingCodec::H264;
            })
        );

        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPixelStreamingToolbar::RegisterMenus));
    }
    
    FPixelStreamingToolbar::~FPixelStreamingToolbar()
    {
        FPixelStreamingCommands::Unregister();
    }
    
    void FPixelStreamingToolbar::RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);
        {
            UToolMenu* CustomToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
            {
                FToolMenuSection& Section = CustomToolBar->AddSection("PixelStreaming");
                Section.AddSeparator("PixelStreamingStart");
                {
                    TSharedRef<SWidget> TitleWidget = SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("Pixel Streaming", "Pixel Streaming"))
                    ];

                    // Play Button
                    FToolMenuEntry TitleEntry = FToolMenuEntry::InitWidget(
                        FName("Title"),
                        TitleWidget,
                        FText()
                    );

                    Section.AddEntry(TitleEntry);  
                }

                {
                    // Play Button
                    FToolMenuEntry PlayEntry = FToolMenuEntry::InitToolBarButton(
                        FPixelStreamingCommands::Get().StartStreaming,
                        TAttribute<FText>(),
                        TAttribute<FText>(),
                        FSlateIcon(FAppStyle::GetAppStyleSetName(), "PlayWorld.PlayInViewport"),
                        FName("StartStreaming")
                    );

                    PlayEntry.StyleNameOverride = FName("Toolbar.BackplateLeftPlay");   
                    PlayEntry.SetCommandList(PluginCommands);
                    Section.AddEntry(PlayEntry);  
                }
                           
                {
                    // Stop Button
                    FToolMenuEntry StopEntry = FToolMenuEntry::InitToolBarButton(
                        FPixelStreamingCommands::Get().StopStreaming,
                        TAttribute<FText>(),
                        TAttribute<FText>(),
                        FSlateIcon(FAppStyle::GetAppStyleSetName(), "PlayWorld.StopPlaySession"),
                        FName("StopStreaming")
                    );

                    StopEntry.StyleNameOverride = FName("Toolbar.BackplateCenterStop");
                    StopEntry.SetCommandList(PluginCommands);
                    Section.AddEntry(StopEntry);
                }

                {
                    // Settings dropdown
                    FToolMenuEntry SettingsEntry = FToolMenuEntry::InitComboButton(
                        "PixelStreamingSettingsMenu",
                        FUIAction(),
                        FOnGetContent::CreateLambda(
                            [&]()
                            {
                                FMenuBuilder MenuBuilder(true, PluginCommands);
                                
                                MenuBuilder.BeginSection("Settings", LOCTEXT("PixelStreamingSettings", "Settings"));
                                {
                                    TSharedRef<SWidget> URLInput = SNew(SEditableTextBox)
                                    .Text_Lambda([&, Module = &PixelStreamingModule]()
                                    {
                                        if(TSharedPtr<IPixelStreamingStreamer> Streamer = Module->GetStreamer(Module->GetDefaultStreamerID()))    
                                        {
                                             return FText::FromString(Streamer->GetSignallingServerURL());
                                        }
                                       return FText::FromString(TEXT("No Streamer configured"));
                                    })
								    .OnTextCommitted_Lambda([&, Module = &PixelStreamingModule](const FText& InText, ETextCommit::Type InTextCommit)
                                    {
                                        if(TSharedPtr<IPixelStreamingStreamer> Streamer = Module->GetStreamer(Module->GetDefaultStreamerID()))    
                                        {
                                            Streamer->SetSignallingServerURL(InText.ToString());
                                        }
                                    });

                                    MenuBuilder.AddWidget(URLInput,  LOCTEXT("PixelStreamingURL", "Signalling URL"), true);
                                }
                                MenuBuilder.EndSection();

                                MenuBuilder.BeginSection("Advanced", LOCTEXT("PixelStreamingSettings", "Advanced"));
                                {
                                    MenuBuilder.AddSubMenu(
                                        LOCTEXT("PixelStreamingCodecSettings", "Select Codec"),
                                        FText(),
                                        FNewMenuDelegate::CreateLambda([](FMenuBuilder& SubMenuBuilder)
                                        {
                                            SubMenuBuilder.AddMenuEntry(FPixelStreamingCommands::Get().H264);
                                            SubMenuBuilder.AddMenuEntry(FPixelStreamingCommands::Get().VP8);
                                            SubMenuBuilder.AddMenuEntry(FPixelStreamingCommands::Get().VP9);
                                        })
                                    );
                                }
                                MenuBuilder.EndSection();

                                return MenuBuilder.MakeWidget();
                            }
                        ),
                        FText(),
                        LOCTEXT("LevelEditorToolbarPixelStreamingSettingsTooltip", "Configure PixelStreaming")
                    );
                    SettingsEntry.StyleNameOverride = FName("Toolbar.BackplateRightCombo");
                    SettingsEntry.SetCommandList(PluginCommands);
                    Section.AddEntry(SettingsEntry);
                }
                Section.AddSeparator("PixelStreamingEnd");
            }
        }
    }

    TSharedRef<SWidget> FPixelStreamingToolbar::GeneratePixelStreamingMenuContent(TSharedPtr<FUICommandList> InCommandList)
    {     
        FToolMenuContext MenuContext(InCommandList);
        return UToolMenus::Get()->GenerateWidget("LevelEditor.LevelEditorToolBar.AddQuickMenu", MenuContext);
    }
}

#undef LOCTEXT_NAMESPACE