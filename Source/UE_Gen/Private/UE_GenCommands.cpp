// Copyright UE_Gen. All Rights Reserved.

#include "UE_GenCommands.h"

#define LOCTEXT_NAMESPACE "FUE_GenModule"

void FUE_GenCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow,
		"UE Gen",
		"Open the UE Gen AI Viewport Generator panel",
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
