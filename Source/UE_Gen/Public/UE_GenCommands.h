// Copyright UE_Gen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

/**
 * Editor commands for UE Gen plugin.
 * Defines the toolbar button and keyboard shortcuts.
 */
class FUE_GenCommands : public TCommands<FUE_GenCommands>
{
public:
	FUE_GenCommands()
		: TCommands<FUE_GenCommands>(
			TEXT("UE_Gen"),
			NSLOCTEXT("Contexts", "UE_Gen", "UE Gen - AI Viewport Generator"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	/** Register all commands */
	virtual void RegisterCommands() override;

	/** Opens the main UE Gen panel */
	TSharedPtr<FUICommandInfo> OpenPluginWindow;

	/** Captures the current viewport */
	TSharedPtr<FUICommandInfo> CaptureViewport;

	/** Triggers generation with current settings */
	TSharedPtr<FUICommandInfo> GenerateImage;
};
