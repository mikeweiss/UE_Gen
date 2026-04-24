// Copyright ViewGen. All Rights Reserved.

#include "ViewGenStyle.h"

#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

TSharedPtr<FSlateStyleSet> FViewGenStyle::StyleInstance = nullptr;

void FViewGenStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FViewGenStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

void FViewGenStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FViewGenStyle::Get()
{
	return *StyleInstance;
}

FName FViewGenStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("ViewGenStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FViewGenStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("ViewGen")->GetBaseDir() / TEXT("Resources"));

	// Set up icon styles - falls back to a default if icon file is missing
	Style->Set("ViewGen.OpenPluginWindow", new FSlateImageBrush(
		Style->RootToContentDir(TEXT("Icon128"), TEXT(".png")), FVector2D(40.0f, 40.0f)));

	// Thumbnail border style
	Style->Set("ViewGen.ThumbnailBorder", new FSlateColorBrush(FLinearColor(0.02f, 0.02f, 0.02f, 1.0f)));

	// Preview area background
	Style->Set("ViewGen.PreviewBackground", new FSlateColorBrush(FLinearColor(0.01f, 0.01f, 0.01f, 1.0f)));

	return Style;
}
