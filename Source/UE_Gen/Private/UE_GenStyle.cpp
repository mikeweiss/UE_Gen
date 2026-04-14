// Copyright UE_Gen. All Rights Reserved.

#include "UE_GenStyle.h"

#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

TSharedPtr<FSlateStyleSet> FUE_GenStyle::StyleInstance = nullptr;

void FUE_GenStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FUE_GenStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

void FUE_GenStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FUE_GenStyle::Get()
{
	return *StyleInstance;
}

FName FUE_GenStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("UE_GenStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FUE_GenStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("UE_Gen")->GetBaseDir() / TEXT("Resources"));

	// Set up icon styles - falls back to a default if icon file is missing
	Style->Set("UE_Gen.OpenPluginWindow", new FSlateImageBrush(
		Style->RootToContentDir(TEXT("Icon128"), TEXT(".png")), FVector2D(40.0f, 40.0f)));

	// Thumbnail border style
	Style->Set("UE_Gen.ThumbnailBorder", new FSlateColorBrush(FLinearColor(0.02f, 0.02f, 0.02f, 1.0f)));

	// Preview area background
	Style->Set("UE_Gen.PreviewBackground", new FSlateColorBrush(FLinearColor(0.01f, 0.01f, 0.01f, 1.0f)));

	return Style;
}
