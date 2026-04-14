// Copyright UE_Gen. All Rights Reserved.

#include "ViewportCapture.h"

#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "EditorViewportClient.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/Base64.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/SceneViewport.h"

FViewportCapture::FViewportCapture()
{
}

FViewportCapture::~FViewportCapture()
{
	ClearCapture();
}

bool FViewportCapture::CaptureActiveViewport(int32 DesiredWidth, int32 DesiredHeight)
{
	ClearCapture();

	// Get the active level editor viewport
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();

	if (!ActiveLevelViewport.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen: No active level viewport found"));
		return false;
	}

	FSceneViewport* SceneViewport = ActiveLevelViewport->GetSceneViewport().Get();
	if (!SceneViewport)
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen: Could not get scene viewport"));
		return false;
	}

	// ---- Extract camera metadata from the viewport client ----
	bHasCameraData = false;
	FEditorViewportClient& ViewportClient = ActiveLevelViewport->GetLevelViewportClient();
	{
		CameraLocation = ViewportClient.GetViewLocation();
		CameraRotation = ViewportClient.GetViewRotation();
		CameraFOV = ViewportClient.ViewFOV;

		// Convert FOV to approximate 35mm-equivalent focal length
		// 35mm full-frame sensor diagonal ~= 43.27mm, sensor width ~= 36mm
		// focal_length = (sensor_width / 2) / tan(hfov / 2)
		const float SensorWidth = 36.0f; // mm, standard full-frame
		float HalfFOVRad = FMath::DegreesToRadians(CameraFOV * 0.5f);
		if (HalfFOVRad > 0.01f)
		{
			FocalLength35mm = (SensorWidth * 0.5f) / FMath::Tan(HalfFOVRad);
		}
		else
		{
			FocalLength35mm = 800.0f; // extreme telephoto fallback
		}

		bHasCameraData = true;

		UE_LOG(LogTemp, Log, TEXT("UE_Gen: Camera FOV=%.1f, ~%dmm equiv, Loc=(%s), Rot=(%s)"),
			CameraFOV, FMath::RoundToInt(FocalLength35mm),
			*CameraLocation.ToString(), *CameraRotation.ToString());
	}

	// Read pixels from the viewport backbuffer
	FIntPoint ViewportSize = SceneViewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen: Viewport has invalid size"));
		return false;
	}

	TArray<FColor> Pixels;
	if (!SceneViewport->ReadPixels(Pixels))
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen: Failed to read viewport pixels"));
		return false;
	}

	// Viewport backbuffers often return alpha=0 for opaque scene content.
	// Force alpha to 255 so the Slate thumbnail renders as fully opaque.
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}

	CaptureWidth = ViewportSize.X;
	CaptureHeight = ViewportSize.Y;
	AspectRatio = (CaptureHeight > 0) ? static_cast<float>(CaptureWidth) / static_cast<float>(CaptureHeight) : 1.0f;

	// Resize if requested
	if (DesiredWidth > 0 && DesiredHeight > 0 &&
		(DesiredWidth != CaptureWidth || DesiredHeight != CaptureHeight))
	{
		TArray<FColor> ResizedPixels;
		ResizedPixels.SetNumUninitialized(DesiredWidth * DesiredHeight);
		FImageUtils::ImageResize(CaptureWidth, CaptureHeight, Pixels,
			DesiredWidth, DesiredHeight, ResizedPixels, false);
		RawPixels = MoveTemp(ResizedPixels);
		CaptureWidth = DesiredWidth;
		CaptureHeight = DesiredHeight;
	}
	else
	{
		RawPixels = MoveTemp(Pixels);
	}

	// Encode to PNG for HTTP transmission
	EncodeToPNG(RawPixels, CaptureWidth, CaptureHeight, PNGData);

	// Create texture for UI display (AddToRoot prevents GC since we're not a UObject)
	CapturedTexture = CreateTextureFromPixels(RawPixels, CaptureWidth, CaptureHeight);
	if (!CapturedTexture)
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen: Failed to create texture from %dx%d viewport pixels (%d pixels)"),
			CaptureWidth, CaptureHeight, RawPixels.Num());
		return false;
	}
	CapturedTexture->AddToRoot();

	UE_LOG(LogTemp, Log, TEXT("UE_Gen: Captured viewport %dx%d (%d bytes PNG)"),
		CaptureWidth, CaptureHeight, PNGData.Num());

	return true;
}

void FViewportCapture::ClearCapture()
{
	// During editor shutdown UObjects may already be destroyed.
	// Use ::IsValid() before touching any UObject to avoid index assertions.
	if (CapturedTexture && ::IsValid(CapturedTexture) && CapturedTexture->IsRooted())
	{
		CapturedTexture->RemoveFromRoot();
	}
	CapturedTexture = nullptr;
	RawPixels.Empty();
	PNGData.Empty();
	CaptureWidth = 0;
	CaptureHeight = 0;
	bHasCameraData = false;
}

FString FViewportCapture::BuildCameraPromptDescription() const
{
	if (!bHasCameraData) return FString();

	TArray<FString> Parts;

	// ---- Focal length / lens type ----
	int32 FocalMM = FMath::RoundToInt(FocalLength35mm);
	if (FocalMM <= 18)
		Parts.Add(FString::Printf(TEXT("shot with ultra-wide %dmm lens"), FocalMM));
	else if (FocalMM <= 35)
		Parts.Add(FString::Printf(TEXT("shot with wide-angle %dmm lens"), FocalMM));
	else if (FocalMM <= 60)
		Parts.Add(FString::Printf(TEXT("shot with %dmm standard lens"), FocalMM));
	else if (FocalMM <= 135)
		Parts.Add(FString::Printf(TEXT("shot with %dmm telephoto lens"), FocalMM));
	else
		Parts.Add(FString::Printf(TEXT("shot with %dmm super-telephoto lens"), FocalMM));

	// ---- Camera pitch / vertical angle ----
	float Pitch = CameraRotation.Pitch;
	if (Pitch < -45.0f)
		Parts.Add(TEXT("extreme high-angle looking down"));
	else if (Pitch < -15.0f)
		Parts.Add(TEXT("high-angle shot looking down"));
	else if (Pitch < -5.0f)
		Parts.Add(TEXT("slightly elevated angle"));
	else if (Pitch > 45.0f)
		Parts.Add(TEXT("extreme low-angle looking up"));
	else if (Pitch > 15.0f)
		Parts.Add(TEXT("low-angle shot looking up"));
	else if (Pitch > 5.0f)
		Parts.Add(TEXT("slightly low angle"));
	else
		Parts.Add(TEXT("eye-level angle"));

	// ---- FOV / perspective distortion ----
	if (CameraFOV > 100.0f)
		Parts.Add(TEXT("strong perspective distortion"));
	else if (CameraFOV > 75.0f)
		Parts.Add(TEXT("moderate perspective"));
	else if (CameraFOV < 30.0f)
		Parts.Add(TEXT("compressed perspective, shallow depth of field"));
	else if (CameraFOV < 50.0f)
		Parts.Add(TEXT("flat compressed perspective"));

	// ---- Aspect ratio ----
	if (AspectRatio > 2.2f)
		Parts.Add(TEXT("ultra-wide cinematic framing"));
	else if (AspectRatio > 1.7f)
		Parts.Add(TEXT("widescreen 16:9 framing"));
	else if (AspectRatio > 1.3f)
		Parts.Add(TEXT("4:3 framing"));
	else if (AspectRatio < 0.7f)
		Parts.Add(TEXT("tall portrait framing"));
	else if (AspectRatio < 0.9f)
		Parts.Add(TEXT("portrait orientation"));

	// ---- Camera height hint (from Z position, rough classification) ----
	float CameraZ = CameraLocation.Z;
	if (CameraZ < 50.0f)
		Parts.Add(TEXT("ground-level camera"));
	else if (CameraZ < 200.0f)
		Parts.Add(TEXT("low camera position"));
	else if (CameraZ > 2000.0f)
		Parts.Add(TEXT("aerial bird's-eye view"));
	else if (CameraZ > 800.0f)
		Parts.Add(TEXT("elevated overhead view"));

	return FString::Join(Parts, TEXT(", "));
}

bool FViewportCapture::EncodeToPNG(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutPNG)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		return false;
	}

	if (!ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor),
		Width, Height, ERGBFormat::BGRA, 8))
	{
		return false;
	}

	OutPNG = ImageWrapper->GetCompressed(100);
	return OutPNG.Num() > 0;
}

FString FViewportCapture::GetBase64PNG() const
{
	if (PNGData.Num() == 0)
	{
		return FString();
	}
	return FBase64::Encode(PNGData);
}

UTexture2D* FViewportCapture::CreateTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;

	// Lock and copy pixel data
	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();

	Texture->UpdateResource();

	return Texture;
}
