// Copyright UE_Gen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;
class SDockTab;

/**
 * Main module for UE Gen - AI Viewport Generator plugin.
 * Registers the editor tab, toolbar extension, and manages plugin lifecycle.
 */
class FUE_GenModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Singleton-like access to this module's interface.
	 * Beware of calling this during the shutdown phase - the module might have been unloaded already.
	 */
	static FUE_GenModule& Get();
	static bool IsAvailable();

private:
	/** Registers the main dockable tab spawner */
	void RegisterTabSpawner();
	void UnregisterTabSpawner();

	/** Callback to spawn the main editor tab */
	TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);

	/** Adds the toolbar button */
	void RegisterMenuExtensions();
	void UnregisterMenuExtensions();

	/** Handle for the toolbar extender delegate */
	TSharedPtr<FExtensibilityManager> ExtensibilityManager;

	/** The tab identifier for our dockable panel */
	static const FName UE_GenTabName;
};
