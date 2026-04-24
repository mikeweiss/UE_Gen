// Copyright ViewGen. All Rights Reserved.

#include "ViewGen.h"
#include "ViewGenStyle.h"
#include "ViewGenCommands.h"
#include "SViewGenPanel.h"

#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
// No cross-plugin dependencies — each plugin registers itself in StoryTools independently

#define LOCTEXT_NAMESPACE "FViewGenModule"

const FName FViewGenModule::ViewGenTabName(TEXT("ViewGen"));

void FViewGenModule::StartupModule()
{
	// Initialize style and commands
	FViewGenStyle::Initialize();
	FViewGenCommands::Register();

	// Map the "Open Window" command to actually open our tab
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ViewGenTabName,
		FOnSpawnTab::CreateRaw(this, &FViewGenModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("TabTitle", "ViewGen"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FViewGenStyle::GetStyleSetName(), "ViewGen.OpenPluginWindow"));

	RegisterMenuExtensions();
}

void FViewGenModule::ShutdownModule()
{
	UnregisterMenuExtensions();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ViewGenTabName);

	FViewGenCommands::Unregister();
	FViewGenStyle::Shutdown();
}

FViewGenModule& FViewGenModule::Get()
{
	return FModuleManager::LoadModuleChecked<FViewGenModule>("ViewGen");
}

bool FViewGenModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("ViewGen");
}

TSharedRef<SDockTab> FViewGenModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SViewGenPanel)
		];
}

void FViewGenModule::RegisterMenuExtensions()
{
	// Bind commands to actions
	TSharedPtr<FUICommandList> PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FViewGenCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(ViewGenTabName);
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
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FViewGenCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}

		// Add menu entry under Window menu
		UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = WindowMenu->FindOrAddSection("ViewGen");
			Section.AddMenuEntryWithCommandList(FViewGenCommands::Get().OpenPluginWindow, PluginCommands);
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

					// ---- ViewGen (this plugin's own entry) ----
					Section.AddMenuEntry(
						FName("OpenUEGen"),
						LOCTEXT("OpenUEGen", "ViewGen"),
						LOCTEXT("OpenUEGenTooltip", "AI Viewport Generator — ComfyUI workflow editor with viewport capture, video generation, and 3D asset import"),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"),
						FExecuteAction::CreateLambda([]()
						{
							FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("ViewGen")));
						})
					);
				})
			);
		}
	}));
}

void FViewGenModule::UnregisterMenuExtensions()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FViewGenModule, ViewGen)
