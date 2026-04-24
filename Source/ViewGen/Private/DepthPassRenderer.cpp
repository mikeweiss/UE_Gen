// Copyright ViewGen. All Rights Reserved.

#include "DepthPassRenderer.h"
#include "ViewportCapture.h"

#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "EditorViewportClient.h"
#include "Slate/SceneViewport.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "RenderingThread.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Kismet/KismetRenderingLibrary.h"

FDepthPassRenderer::FDepthPassRenderer()
{
}

FDepthPassRenderer::~FDepthPassRenderer()
{
	ClearCapture();
}

bool FDepthPassRenderer::CaptureDepth(float MaxDepthDistance, int32 DesiredWidth, int32 DesiredHeight)
{
	ClearCapture();
	LastMaxDepthDistance = MaxDepthDistance;

	// Get the active viewport to match its camera
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();

	if (!ActiveLevelViewport.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Depth: No active level viewport found"));
		return false;
	}

	FEditorViewportClient& ViewportClient = ActiveLevelViewport->GetLevelViewportClient();
	FSceneViewport* SceneViewport = ActiveLevelViewport->GetSceneViewport().Get();

	if (!SceneViewport)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Depth: Could not get scene viewport"));
		return false;
	}

	FIntPoint ViewportSize = SceneViewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return false;
	}

	// Determine output size
	int32 CaptureWidth = (DesiredWidth > 0) ? DesiredWidth : ViewportSize.X;
	int32 CaptureHeight = (DesiredHeight > 0) ? DesiredHeight : ViewportSize.Y;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Depth: No editor world available"));
		return false;
	}

	// Get camera transform from viewport
	FVector CameraLocation = ViewportClient.GetViewLocation();
	FRotator CameraRotation = ViewportClient.GetViewRotation();
	float CameraFOV = ViewportClient.ViewFOV;

	UE_LOG(LogTemp, Log, TEXT("ViewGen Depth: Camera at (%.1f, %.1f, %.1f) Rot (%.1f, %.1f, %.1f) FOV %.1f"),
		CameraLocation.X, CameraLocation.Y, CameraLocation.Z,
		CameraRotation.Pitch, CameraRotation.Yaw, CameraRotation.Roll,
		CameraFOV);

	// Create a render target for the depth capture
	UTextureRenderTarget2D* DepthRT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	DepthRT->RenderTargetFormat = RTF_RGBA16f;
	DepthRT->ClearColor = FLinearColor::Black;
	DepthRT->bAutoGenerateMips = false;
	DepthRT->InitAutoFormat(CaptureWidth, CaptureHeight);
	DepthRT->UpdateResourceImmediate(true);

	// Spawn a temporary actor at the camera position
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	AActor* CaptureActor = World->SpawnActor<AActor>(AActor::StaticClass(),
		FTransform(CameraRotation, CameraLocation), SpawnParams);
	if (!CaptureActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Depth: Failed to spawn capture actor"));
		return false;
	}

	// Create a root scene component so the actor has a valid transform hierarchy
	USceneComponent* RootComp = NewObject<USceneComponent>(CaptureActor, TEXT("Root"));
	CaptureActor->SetRootComponent(RootComp);
	RootComp->RegisterComponent();
	RootComp->SetWorldLocationAndRotation(CameraLocation, CameraRotation);

	// Add a SceneCaptureComponent2D attached to the root
	USceneCaptureComponent2D* CaptureComp = NewObject<USceneCaptureComponent2D>(CaptureActor, TEXT("DepthCapture"));
	CaptureComp->SetupAttachment(RootComp);
	CaptureComp->RegisterComponent();

	// Set the capture component's world transform explicitly (belt and suspenders)
	CaptureComp->SetWorldLocationAndRotation(CameraLocation, CameraRotation);

	// Configure for scene depth capture
	CaptureComp->TextureTarget = DepthRT;
	CaptureComp->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	CaptureComp->bCaptureEveryFrame = false;
	CaptureComp->bCaptureOnMovement = false;
	CaptureComp->bAlwaysPersistRenderingState = false;
	CaptureComp->FOVAngle = CameraFOV;

	UE_LOG(LogTemp, Log, TEXT("ViewGen Depth: CaptureComp world location: (%.1f, %.1f, %.1f)"),
		CaptureComp->GetComponentLocation().X,
		CaptureComp->GetComponentLocation().Y,
		CaptureComp->GetComponentLocation().Z);

	// Capture the scene depth
	CaptureComp->CaptureScene();

	// Flush rendering to ensure the capture is complete
	FlushRenderingCommands();

	// Read the depth render target and convert to grayscale
	TArray<FColor> GrayscalePixels = ReadAndConvertDepthRT(DepthRT, MaxDepthDistance);

	// Clean up the temporary actor
	CaptureActor->Destroy();

	if (GrayscalePixels.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Depth: Failed to read depth render target"));
		return false;
	}

	DepthWidth = CaptureWidth;
	DepthHeight = CaptureHeight;

	// Encode to PNG
	FViewportCapture::EncodeToPNG(GrayscalePixels, DepthWidth, DepthHeight, DepthPNGData);

	// Create display texture (AddToRoot prevents GC since we're not a UObject)
	DepthTexture = CreateDepthTextureFromPixels(GrayscalePixels, DepthWidth, DepthHeight);
	if (DepthTexture)
	{
		DepthTexture->AddToRoot();
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen Depth: Captured scene depth %dx%d (max depth: %.0f UU)"),
		DepthWidth, DepthHeight, MaxDepthDistance);

	return DepthTexture != nullptr;
}

TArray<FColor> FDepthPassRenderer::ReadAndConvertDepthRT(UTextureRenderTarget2D* RT, float MaxDepthDistance)
{
	TArray<FColor> GrayscalePixels;

	if (!RT || !RT->GetResource())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Depth: RT or RT resource is null"));
		return GrayscalePixels;
	}

	int32 Width = RT->SizeX;
	int32 Height = RT->SizeY;

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Depth: RTResource is null"));
		return GrayscalePixels;
	}

	// Read raw depth values - use ReadLinearColorPixels as primary (more reliable)
	TArray<FLinearColor> LinearPixels;
	FReadSurfaceDataFlags ReadFlags(RCM_MinMax);
	RTResource->ReadLinearColorPixels(LinearPixels, ReadFlags);

	UE_LOG(LogTemp, Log, TEXT("ViewGen Depth: ReadLinearColorPixels returned %d pixels (expected %d)"),
		LinearPixels.Num(), Width * Height);

	if (LinearPixels.Num() != Width * Height)
	{
		// Try ReadFloat16Pixels as fallback
		TArray<FFloat16Color> FloatPixels;
		bool bReadSuccess = RTResource->ReadFloat16Pixels(FloatPixels);
		UE_LOG(LogTemp, Log, TEXT("ViewGen Depth: ReadFloat16Pixels returned %d pixels, success=%d"),
			FloatPixels.Num(), bReadSuccess ? 1 : 0);

		if (!bReadSuccess || FloatPixels.Num() != Width * Height)
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen Depth: Both pixel read methods failed"));
			return GrayscalePixels;
		}

		LinearPixels.SetNumUninitialized(Width * Height);
		for (int32 i = 0; i < FloatPixels.Num(); i++)
		{
			LinearPixels[i] = FLinearColor(
				FloatPixels[i].R.GetFloat(),
				FloatPixels[i].G.GetFloat(),
				FloatPixels[i].B.GetFloat(),
				FloatPixels[i].A.GetFloat()
			);
		}
	}

	// Diagnostic: log sample depth values at various positions
	auto LogSample = [&](const TCHAR* Label, int32 X, int32 Y)
	{
		if (X >= 0 && X < Width && Y >= 0 && Y < Height)
		{
			int32 Idx = Y * Width + X;
			const FLinearColor& P = LinearPixels[Idx];
			UE_LOG(LogTemp, Log, TEXT("ViewGen Depth Sample [%s] (%d,%d): R=%.4f G=%.4f B=%.4f A=%.4f"),
				Label, X, Y, P.R, P.G, P.B, P.A);
		}
	};

	LogSample(TEXT("TopLeft"), 0, 0);
	LogSample(TEXT("TopRight"), Width - 1, 0);
	LogSample(TEXT("Center"), Width / 2, Height / 2);
	LogSample(TEXT("BottomLeft"), 0, Height - 1);
	LogSample(TEXT("BottomRight"), Width - 1, Height - 1);
	LogSample(TEXT("Quarter"), Width / 4, Height / 4);
	LogSample(TEXT("ThreeQuarter"), Width * 3 / 4, Height * 3 / 4);

	// Extract depth from R channel (SCS_SceneDepth writes world-space distance to R)
	TArray<float> RawDepths;
	RawDepths.SetNumUninitialized(Width * Height);
	for (int32 i = 0; i < LinearPixels.Num(); i++)
	{
		RawDepths[i] = LinearPixels[i].R;
	}

	// Count zero, negative, and positive values for diagnostics
	int32 ZeroCount = 0, NegativeCount = 0, PositiveCount = 0, InfCount = 0;
	float SumPositive = 0.0f;
	for (float D : RawDepths)
	{
		if (D == 0.0f) ZeroCount++;
		else if (D < 0.0f) NegativeCount++;
		else if (D >= 1e10f || !FMath::IsFinite(D)) InfCount++;
		else { PositiveCount++; SumPositive += D; }
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen Depth: Pixel stats - Zero: %d, Negative: %d, Positive: %d, Inf/NaN: %d, Total: %d"),
		ZeroCount, NegativeCount, PositiveCount, InfCount, RawDepths.Num());

	if (PositiveCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen Depth: Average positive depth: %.2f"), SumPositive / PositiveCount);
	}

	// ---- Logarithmic depth normalization ----
	// Linear normalization produces harsh, binary-looking depth maps because
	// world-space depth spans huge ranges. Logarithmic mapping compresses the
	// far range and expands the near range, producing smooth gradients similar
	// to MiDaS / ControlNet depth maps.

	const float SkyThreshold = FMath::Max(MaxDepthDistance, 100000.0f);

	// Collect all valid (non-sky) depth values for percentile-based clamping
	TArray<float> ValidDepths;
	ValidDepths.Reserve(RawDepths.Num() / 2);

	for (float D : RawDepths)
	{
		if (D > 0.0f && D < SkyThreshold && FMath::IsFinite(D))
		{
			ValidDepths.Add(D);
		}
	}

	if (ValidDepths.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Depth: No valid depth pixels found"));
		return GrayscalePixels;
	}

	// Sort to find percentiles — clip the extreme 1% on each end to remove outliers
	ValidDepths.Sort();

	int32 LowIdx  = FMath::Clamp(ValidDepths.Num() / 100, 0, ValidDepths.Num() - 1);       // 1st percentile
	int32 HighIdx = FMath::Clamp(ValidDepths.Num() * 99 / 100, 0, ValidDepths.Num() - 1);   // 99th percentile

	float NearClip = FMath::Max(ValidDepths[LowIdx], 1.0f);   // avoid log(0)
	float FarClip  = ValidDepths[HighIdx];

	if (FarClip <= NearClip)
	{
		FarClip = NearClip + 1.0f;
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen Depth: Valid pixels: %d, Percentile range: %.1f - %.1f UU (of raw %.1f - %.1f)"),
		ValidDepths.Num(), NearClip, FarClip, ValidDepths[0], ValidDepths.Last());

	// Logarithmic mapping: log(depth) mapped to [0, 1]
	float LogNear = FMath::Loge(NearClip);
	float LogFar  = FMath::Loge(FarClip);
	float LogRange = FMath::Max(LogFar - LogNear, 0.001f);

	// Convert to grayscale: close = white, far = black
	GrayscalePixels.SetNumUninitialized(Width * Height);
	for (int32 i = 0; i < RawDepths.Num(); i++)
	{
		float Depth = RawDepths[i];

		// Sky/infinity pixels → black
		if (Depth <= 0.0f || Depth >= SkyThreshold || !FMath::IsFinite(Depth))
		{
			GrayscalePixels[i] = FColor(0, 0, 0, 255);
			continue;
		}

		// Clamp to the percentile range, then apply log mapping
		float ClampedDepth = FMath::Clamp(Depth, NearClip, FarClip);
		float LogDepth = FMath::Loge(ClampedDepth);
		float Normalized = FMath::Clamp((LogDepth - LogNear) / LogRange, 0.0f, 1.0f);

		// Invert: close = white (255), far = black (0)
		uint8 GrayValue = static_cast<uint8>((1.0f - Normalized) * 255.0f);
		GrayscalePixels[i] = FColor(GrayValue, GrayValue, GrayValue, 255);
	}

	// Count output grayscale distribution for diagnostics
	int32 BlackPixels = 0, WhitePixels = 0, MidPixels = 0;
	for (const FColor& C : GrayscalePixels)
	{
		if (C.R == 0) BlackPixels++;
		else if (C.R == 255) WhitePixels++;
		else MidPixels++;
	}
	UE_LOG(LogTemp, Log, TEXT("ViewGen Depth: Output - Black: %d, White: %d, Mid-gray: %d"),
		BlackPixels, WhitePixels, MidPixels);

	return GrayscalePixels;
}

void FDepthPassRenderer::ClearCapture()
{
	// During editor shutdown UObjects may already be destroyed.
	// Use ::IsValid() before touching any UObject to avoid index assertions.
	if (DepthTexture && ::IsValid(DepthTexture) && DepthTexture->IsRooted())
	{
		DepthTexture->RemoveFromRoot();
	}
	DepthTexture = nullptr;
	DepthPNGData.Empty();
	DepthWidth = 0;
	DepthHeight = 0;
}

FString FDepthPassRenderer::GetBase64PNG() const
{
	if (DepthPNGData.Num() == 0)
	{
		return FString();
	}
	return FBase64::Encode(DepthPNGData);
}

UTexture2D* FDepthPassRenderer::CreateDepthTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->SRGB = false; // Depth should be linear
	Texture->Filter = TF_Bilinear;

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();

	Texture->UpdateResource();
	Texture->AddToRoot(); // Prevent GC — caller must RemoveFromRoot when done

	return Texture;
}
