// Copyright UE_Gen. All Rights Reserved.

#include "UE_Gen.h"
#include "UE_GenStyle.h"
#include "UE_GenCommands.h"
#include "SUE_GenPanel.h"

#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
// No cross-plugin dependencies — each plugin registers itself in StoryTools independently

#define LOCTEXT_NAMESPACE "FUE_GenModule"

const FName FUE_GenModule::UE_GenTabName(TEXT("UE_Gen"));

void FUE_GenModule::StartupModule()
{
	// Initialize style and commands
	FUE_GenStyle::Initialize();
	FUE_GenCommands::Register();

	// Map the "Open Window" command to actually open our tab
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		UE_GenTabName,
		FOnSpawnTab::CreateRaw(this, &FUE_GenModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("TabTitle", "UE Gen"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FUE_GenStyle::GetStyleSetName(), "UE_Gen.OpenPluginWindow"));

	RegisterMenuExtensions();
}

void FUE_GenModule::ShutdownModule()
{
	UnregisterMenuExtensions();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UE_GenTabName);

	FUE_GenCommands::Unregister();
	FUE_GenStyle::Shutdown();
}

FUE_GenModule& FUE_GenModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUE_GenModule>("UE_Gen");
}

bool FUE_GenModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("UE_Gen");
}

TSharedRef<SDockTab> FUE_GenModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SUE_GenPanel)
		];
}

void FUE_GenModule::RegisterMenuExtensions()
{
	// Bind commands to actions
	TSharedPtr<FUICommandList> PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FUE_GenCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UE_GenTabName);
		}),
		FCanExecuteAction());

	// Register in the Tool Menus system
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([PluginCommands]()
	{
		// Add toolbar button
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FUE_GenCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}

		// Add menu entry under Window menu
		UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = WindowMenu->FindOrAddSection("UE_Gen");
			Section.AddMenuEntryWithCommandList(FUE_GenCommands::Get().OpenPluginWindow, PluginCommands);
		}

		// ================================================================
		// StoryTools — top-level menu bar entry
		// Each plugin finds-or-creates this menu and adds only its own
		// entry, so there are zero cross-plugin dependencies.
		// ================================================================
		UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
		if (MainMenu)
		{
			FToolMenuSection& StorySection = MainMenu->FindOrAddSection(
				"StoryTools",
				LOCTEXT("StoryToolsMenuLabel", "StoryTools"));

			StorySection.AddSubMenu(
				"StoryToolsSubMenu",
				LOCTEXT("StoryToolsLabel", "StoryTools"),
				LOCTEXT("StoryToolsTooltip", "Story-driven creative toolset"),
				FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
				{
					FToolMenuSection& Section = SubMenu->FindOrAddSection("StoryToolsEntries", LOCTEXT("ToolsSection", "Tools"));

					// ---- UE Gen (this plugin's own entry) ----
					Section.AddMenuEntry(
						FName("OpenUEGen"),
						LOCTEXT("OpenUEGen", "UE Gen"),
						LOCTEXT("OpenUEGenTooltip", "AI Viewport Generator — ComfyUI workflow editor with viewport capture, video generation, and 3D asset import"),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"),
						FExecuteAction::CreateLambda([]()
						{
							FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("UE_Gen")));
						})
					);
				})
			);
		}
	}));
}

void FUE_GenModule::UnregisterMenuExtensions()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUE_GenModule, UE_Gen)
