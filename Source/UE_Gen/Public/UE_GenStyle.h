// Copyright UE_Gen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * Manages the Slate style set for UE Gen plugin icons and visuals.
 */
class FUE_GenStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static void ReloadTextures();

	static const ISlateStyle& Get();
	static FName GetStyleSetName();

private:
	static TSharedRef<FSlateStyleSet> Create();
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};
