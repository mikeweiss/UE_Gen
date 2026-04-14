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
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
// GaussianSplatGeneratorEditor accessed via FModuleManager (no direct header link needed)

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
		// ================================================================
		UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
		if (MainMenu)
		{
			FToolMenuSection& StorySection = MainMenu->AddSection(
				"StoryTools",
				LOCTEXT("StoryToolsMenuLabel", "StoryTools"));

			StorySection.AddSubMenu(
				"StoryToolsSubMenu",
				LOCTEXT("StoryToolsLabel", "StoryTools"),
				LOCTEXT("StoryToolsTooltip", "Story-driven toolset: AI generation, scene analysis, and Gaussian Splats"),
				FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
				{
					FToolMenuSection& Section = SubMenu->AddSection("StoryToolsEntries", LOCTEXT("ToolsSection", "Tools"));

					// ---- UE Gen ----
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

					// ---- Scene Break ----
					Section.AddMenuEntry(
						FName("OpenSceneBreak"),
						LOCTEXT("OpenSceneBreak", "Scene Break"),
						LOCTEXT("OpenSceneBreakTooltip", "AI-powered scene analysis, reference search, and shot breakdown"),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"),
						FExecuteAction::CreateLambda([]()
						{
							// Ensure the SceneBreak module is loaded (registers its NomadTabSpawner)
							if (!FModuleManager::Get().IsModuleLoaded("SceneBreak"))
							{
								FModuleManager::Get().LoadModule("SceneBreak");
							}

							if (FModuleManager::Get().IsModuleLoaded("SceneBreak"))
							{
								FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("SceneBreak")));
							}
							else
							{
								UE_LOG(LogTemp, Warning, TEXT("StoryTools: SceneBreak plugin is not available. Enable it in Edit > Plugins."));
							}
						})
					);

					// ---- Gaussian Splat Generator (submenu) ----
					Section.AddSubMenu(
						"GaussianSplatsSubMenu",
						LOCTEXT("GaussianSplats", "Gaussian Splats"),
						LOCTEXT("GaussianSplatsTooltip", "Generate 3D Gaussian Splats from images or import PLY files"),
						FNewToolMenuDelegate::CreateLambda([](UToolMenu* SplatMenu)
						{
							FToolMenuSection& SplatSection = SplatMenu->AddSection("SplatActions", LOCTEXT("SplatActionsSection", "Actions"));

							SplatSection.AddMenuEntry(
								FName("SplatGenerateFromImage"),
								LOCTEXT("SplatGenerate", "Generate from Image..."),
								LOCTEXT("SplatGenerateTooltip", "Select an image and run Apple SHARP to generate a 3D Gaussian Splat asset"),
								FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Texture2D"),
								FExecuteAction::CreateLambda([]()
								{
									if (!FModuleManager::Get().IsModuleLoaded("GaussianSplatGeneratorEditor"))
									{
										FModuleManager::Get().LoadModule("GaussianSplatGeneratorEditor");
									}

									IModuleInterface* Module = FModuleManager::Get().GetModule("GaussianSplatGeneratorEditor");
									if (Module)
									{
										// Call OnGenerateFromImage() via the ToolMenus command that the module registered
										// The module registers a menu entry at "LevelEditor.MainMenu.GaussianSplats"
										// We replicate the file dialog + import flow inline instead:
										IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
										if (!DesktopPlatform) return;

										TArray<FString> SelectedFiles;
										bool bOpened = DesktopPlatform->OpenFileDialog(
											FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
											TEXT("Select Source Image for Gaussian Splat Generation"),
											FPaths::GetProjectFilePath(),
											TEXT(""),
											TEXT("Image Files (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg"),
											EFileDialogFlags::None,
											SelectedFiles);

										if (bOpened && SelectedFiles.Num() > 0)
										{
											IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
											// Import via the GaussianSplatImageFactory (registered by the module)
											AssetTools.ImportAssets({SelectedFiles[0]}, TEXT("/Game/GaussianSplats"));
										}
									}
									else
									{
										UE_LOG(LogTemp, Warning, TEXT("StoryTools: GaussianSplatGenerator plugin is not available. Enable it in Edit > Plugins."));
									}
								})
							);

							SplatSection.AddMenuEntry(
								FName("SplatImportPLY"),
								LOCTEXT("SplatImportPLY", "Import PLY File..."),
								LOCTEXT("SplatImportPLYTooltip", "Import an existing .ply Gaussian Splat file as a UGaussianSplatAsset"),
								FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.StaticMesh"),
								FExecuteAction::CreateLambda([]()
								{
									// Replicate the PLY import logic directly since it's just a file dialog + asset import
									IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
									if (!DesktopPlatform || !GEditor) return;

									TArray<FString> SelectedFiles;
									bool bOpened = DesktopPlatform->OpenFileDialog(
										FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
										TEXT("Select Gaussian Splat PLY File"),
										FPaths::GetProjectFilePath(),
										TEXT(""),
										TEXT("PLY Files (*.ply)|*.ply"),
										EFileDialogFlags::None,
										SelectedFiles);

									if (bOpened && SelectedFiles.Num() > 0)
									{
										IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
										TArray<FString> FilesToImport;
										FilesToImport.Add(SelectedFiles[0]);
										AssetTools.ImportAssets(FilesToImport, TEXT("/Game/GaussianSplats"));
									}
								})
							);
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
