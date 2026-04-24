// Copyright ViewGen. All Rights Reserved.

#include "ViewGenCommands.h"

#define LOCTEXT_NAMESPACE "FViewGenModule"

void FViewGenCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow,
		"ViewGen",
		"Open the ViewGen AI Viewport Generator panel",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::G));

	UI_COMMAND(CaptureViewport,
		"Capture Viewport",
		"Capture the current editor viewport as input for AI generation",
		EUserInterfaceActionType::Button,
		FInputChord());

	UI_COMMAND(GenerateImage,
		"Generate",
		"Send the captured viewport and depth to the AI backend for generation",
		EUserInterfaceActionType::Button,
		FInputChord());
}

#undef LOCTEXT_NAMESPACE
