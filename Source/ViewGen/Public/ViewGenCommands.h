// Copyright ViewGen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

/**
 * Editor commands for ViewGen plugin.
 * Defines the toolbar button and keyboard shortcuts.
 */
class FViewGenCommands : public TCommands<FViewGenCommands>
{
public:
	FViewGenCommands()
		: TCommands<FViewGenCommands>(
			TEXT("ViewGen"),
			NSLOCTEXT("Contexts", "ViewGen", "ViewGen - AI Viewport Generator"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	/** Register all commands */
	virtual void RegisterCommands() override;

	/** Opens the main ViewGen panel */
	TSharedPtr<FUICommandInfo> OpenPluginWindow;

	/** Captures the current viewport */
	TSharedPtr<FUICommandInfo> CaptureViewport;

	/** Triggers generation with current settings */
	TSharedPtr<FUICommandInfo> GenerateImage;
};
