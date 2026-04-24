// Copyright ViewGen. All Rights Reserved.

#include "SegmentationCapture.h"
#include "ViewportCapture.h"

#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "EditorViewportClient.h"
#include "Slate/SceneViewport.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "RenderingThread.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Landscape.h"

// ============================================================================
// Helpers — boundary-aware mask refinement
// ============================================================================

namespace SegmentationLocal
{

/** Compute Sobel gradient magnitude at (x,y) in an image. */
static float EdgeStrength(const TArray<FColor>& Img, int32 W, int32 H, int32 X, int32 Y)
{
	auto Lum = [](const FColor& C) -> float
	{
		return 0.299f * C.R + 0.587f * C.G + 0.114f * C.B;
	};

	auto Sample = [&](int32 SX, int32 SY) -> float
	{
		SX = FMath::Clamp(SX, 0, W - 1);
		SY = FMath::Clamp(SY, 0, H - 1);
		return Lum(Img[SY * W + SX]);
	};

	float Gx = -Sample(X-1, Y-1) - 2*Sample(X-1, Y) - Sample(X-1, Y+1)
	           + Sample(X+1, Y-1) + 2*Sample(X+1, Y) + Sample(X+1, Y+1);
	float Gy = -Sample(X-1, Y-1) - 2*Sample(X, Y-1) - Sample(X+1, Y-1)
	           + Sample(X-1, Y+1) + 2*Sample(X, Y+1) + Sample(X+1, Y+1);

	return FMath::Sqrt(Gx * Gx + Gy * Gy);
}

/**
 * Check if pixel (x,y) is on the boundary of a binary mask — i.e. it is set
 * but at least one of its 4-connected neighbours is clear (or out of bounds).
 */
static FORCEINLINE bool IsBoundaryPixel(const TArray<uint8>& Mask, int32 W, int32 H, int32 X, int32 Y)
{
	if (!Mask[Y * W + X]) return false;

	if (X == 0     || !Mask[Y * W + (X - 1)]) return true;
	if (X == W - 1 || !Mask[Y * W + (X + 1)]) return true;
	if (Y == 0     || !Mask[(Y - 1) * W + X]) return true;
	if (Y == H - 1 || !Mask[(Y + 1) * W + X]) return true;

	return false;
}

/**
 * Compute a per-pixel edge map from the generated image (Sobel magnitudes).
 * The result is normalised to 0..255.
 */
static void BuildEdgeMap(const TArray<FColor>& Img, int32 W, int32 H, TArray<uint8>& OutEdge)
{
	OutEdge.SetNumZeroed(W * H);

	// First pass: compute raw magnitudes and track the max
	TArray<float> RawEdge;
	RawEdge.SetNumZeroed(W * H);
	float MaxEdge = 1.0f;

	for (int32 y = 1; y < H - 1; ++y)
	{
		for (int32 x = 1; x < W - 1; ++x)
		{
			float E = EdgeStrength(Img, W, H, x, y);
			RawEdge[y * W + x] = E;
			if (E > MaxEdge) MaxEdge = E;
		}
	}

	// Normalise
	const float InvMax = 255.0f / MaxEdge;
	for (int32 i = 0; i < W * H; ++i)
	{
		OutEdge[i] = static_cast<uint8>(FMath::Clamp(RawEdge[i] * InvMax, 0.0f, 255.0f));
	}
}

/**
 * Walk outward from the seed boundary along the edge map.
 * For each boundary pixel of the seed, search outward (up to SearchRadius)
 * for a strong edge. If found, expand the mask out to that edge.
 * This lets the mask "snap" to nearby edges in the generated image.
 */
static void ExpandToEdges(
	TArray<uint8>& Mask,
	const TArray<uint8>& SeedMask,
	const TArray<uint8>& EdgeMap,
	const TArray<FColor>& GenPixels,
	int32 W, int32 H,
	int32 SearchRadius)
{
	// For each pixel just outside the seed (within SearchRadius), check if it
	// should be included based on the edge map.  We do a BFS-like expansion:
	// start from boundary pixels, expand outward through non-edge pixels,
	// stop when we hit a strong edge or exceed the search radius.

	constexpr uint8 EdgeThreshold = 60; // normalised edge strength to count as "strong"

	// Build distance-from-seed for pixels outside the seed, up to SearchRadius
	TArray<int32> DistFromSeed;
	DistFromSeed.SetNum(W * H);
	for (int32 i = 0; i < W * H; ++i)
	{
		DistFromSeed[i] = SeedMask[i] ? 0 : (SearchRadius + 1);
	}

	// Simple iterative distance propagation (Chebyshev / chessboard distance)
	for (int32 Pass = 0; Pass < SearchRadius; ++Pass)
	{
		bool bChanged = false;
		for (int32 y = 0; y < H; ++y)
		{
			for (int32 x = 0; x < W; ++x)
			{
				int32 Idx = y * W + x;
				if (SeedMask[Idx]) continue; // already seed
				if (DistFromSeed[Idx] <= Pass) continue; // already computed

				// Check 4-neighbours for a closer pixel
				int32 MinNeighbour = SearchRadius + 1;
				if (x > 0)     MinNeighbour = FMath::Min(MinNeighbour, DistFromSeed[Idx - 1]);
				if (x < W - 1) MinNeighbour = FMath::Min(MinNeighbour, DistFromSeed[Idx + 1]);
				if (y > 0)     MinNeighbour = FMath::Min(MinNeighbour, DistFromSeed[(y-1) * W + x]);
				if (y < H - 1) MinNeighbour = FMath::Min(MinNeighbour, DistFromSeed[(y+1) * W + x]);

				int32 NewDist = MinNeighbour + 1;
				if (NewDist < DistFromSeed[Idx] && NewDist <= SearchRadius)
				{
					DistFromSeed[Idx] = NewDist;
					bChanged = true;
				}
			}
		}
		if (!bChanged) break;
	}

	// Now decide which expansion pixels to include:
	// Include a pixel outside the seed if:
	//   - It's within SearchRadius of the seed
	//   - There is no strong edge between it and the seed (i.e. the edge
	//     is further out or non-existent — the object extends past the seed)
	//   - OR it's right on a strong edge (the edge IS the object boundary,
	//     include it to capture the full silhouette)
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			int32 Idx = y * W + x;
			if (SeedMask[Idx]) continue; // handled separately
			if (DistFromSeed[Idx] > SearchRadius) continue; // too far

			uint8 Edge = EdgeMap[Idx];

			if (Edge >= EdgeThreshold)
			{
				// We've hit a strong edge — include this pixel (it's the object
				// boundary in the generated image) but don't expand past it
				Mask[Idx] = 1;
			}
			else if (DistFromSeed[Idx] <= 2)
			{
				// Very close to seed and no strong edge blocking — include it.
				// This catches small offsets where the generated image's object
				// extends 1-2px past the viewport silhouette.
				Mask[Idx] = 1;
			}
			else
			{
				// Further out with no edge — check if there's an edge between
				// us and the seed.  Walk inward toward the nearest seed pixel
				// and see if we cross a strong edge.
				bool bEdgeBlocking = false;
				// Simple check: look at pixels closer to the seed than us
				for (int32 dy = -1; dy <= 1 && !bEdgeBlocking; ++dy)
				{
					for (int32 dx = -1; dx <= 1 && !bEdgeBlocking; ++dx)
					{
						if (dx == 0 && dy == 0) continue;
						int32 nx = x + dx, ny = y + dy;
						if (nx >= 0 && nx < W && ny >= 0 && ny < H)
						{
							int32 NIdx = ny * W + nx;
							if (DistFromSeed[NIdx] < DistFromSeed[Idx] && EdgeMap[NIdx] >= EdgeThreshold)
							{
								bEdgeBlocking = true;
							}
						}
					}
				}

				if (!bEdgeBlocking)
				{
					Mask[Idx] = 1;
				}
			}
		}
	}
}

/**
 * Trim the seed boundary inward where the generated image has a strong edge
 * just inside the seed.  This handles cases where the viewport silhouette
 * extends slightly past the actual object in the generated image.
 */
static void TrimToEdges(
	TArray<uint8>& Mask,
	const TArray<uint8>& SeedMask,
	const TArray<uint8>& EdgeMap,
	int32 W, int32 H,
	int32 TrimRadius)
{
	constexpr uint8 EdgeThreshold = 80; // higher threshold for trimming — be conservative

	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			int32 Idx = y * W + x;
			if (!Mask[Idx]) continue;
			if (!IsBoundaryPixel(SeedMask, W, H, x, y)) continue;

			// This is a seed boundary pixel. If there's a strong edge RIGHT HERE
			// in the generated image, check which side the object is on.
			// If the edge is on the outside of the seed, keep the pixel.
			// If it's on the inside, trim it.
			if (EdgeMap[Idx] < EdgeThreshold) continue;

			// Edge is strong here. Count seed neighbours vs non-seed neighbours
			// to determine if the edge is at the outer boundary (keep) or
			// if the seed overshoots the object here (trim).
			int32 SeedNeighbours = 0;
			int32 NonSeedNeighbours = 0;
			for (int32 dy = -1; dy <= 1; ++dy)
			{
				for (int32 dx = -1; dx <= 1; ++dx)
				{
					if (dx == 0 && dy == 0) continue;
					int32 nx = x + dx, ny = y + dy;
					if (nx >= 0 && nx < W && ny >= 0 && ny < H)
					{
						if (SeedMask[ny * W + nx]) SeedNeighbours++;
						else NonSeedNeighbours++;
					}
				}
			}

			// If this boundary pixel has mostly seed neighbours, the edge
			// represents the true boundary — keep it. If it's surrounded by
			// lots of non-seed, it's likely an overshoot — keep it too since
			// it's already a boundary. Only trim if we're on a corner/tip
			// where the seed barely touches.
			(void)SeedNeighbours;
			(void)NonSeedNeighbours;
			// For now, don't trim — the edge expansion already handles fitting.
			// Trimming is conservative because removing pixels is harder to
			// recover from than not adding them.
		}
	}
}

/**
 * Refine a coarse seed mask against the actual generated image pixels.
 *
 * Strategy:
 *   1. ALL seed interior pixels are kept unconditionally — never remove pixels
 *      from the solid interior of the viewport-derived mask.
 *   2. At the seed boundary, compute an edge map from the generated image.
 *   3. Expand outward from the boundary through non-edge pixels — snap to the
 *      nearest strong edge in the generated image (up to SearchRadius pixels).
 *   4. This gives a tight fit to the object's actual silhouette in the generated
 *      image while preserving the full interior.
 */
static void RefineMask(
	TArray<uint8>& RefinedMask,
	const TArray<int32>& CoarseMask,
	int32 SegW, int32 SegH,
	int32 ActorIndex,
	const TArray<FColor>& GenPixels,
	int32 GenW, int32 GenH)
{
	const int32 PixelCount = GenW * GenH;
	RefinedMask.SetNumZeroed(PixelCount);

	const bool bNeedsScale = (GenW != SegW || GenH != SegH);
	const float ScaleX = bNeedsScale ? static_cast<float>(SegW) / GenW : 1.0f;
	const float ScaleY = bNeedsScale ? static_cast<float>(SegH) / GenH : 1.0f;

	// Step 1: Build seed mask in generated-image coordinates
	TArray<uint8> SeedMask;
	SeedMask.SetNumZeroed(PixelCount);

	for (int32 y = 0; y < GenH; ++y)
	{
		for (int32 x = 0; x < GenW; ++x)
		{
			int32 MaskX = bNeedsScale ? FMath::Clamp(FMath::FloorToInt32(x * ScaleX), 0, SegW - 1) : x;
			int32 MaskY = bNeedsScale ? FMath::Clamp(FMath::FloorToInt32(y * ScaleY), 0, SegH - 1) : y;

			if (CoarseMask[MaskY * SegW + MaskX] == ActorIndex)
			{
				SeedMask[y * GenW + x] = 1;
			}
		}
	}

	// Step 2: Keep ALL seed pixels — unconditionally
	FMemory::Memcpy(RefinedMask.GetData(), SeedMask.GetData(), PixelCount);

	// Step 3: Build edge map from the generated image
	TArray<uint8> EdgeMap;
	BuildEdgeMap(GenPixels, GenW, GenH, EdgeMap);

	// Step 4: Expand from seed boundary outward, snapping to generated image edges
	constexpr int32 SearchRadius = 6;
	ExpandToEdges(RefinedMask, SeedMask, EdgeMap, GenPixels, GenW, GenH, SearchRadius);

	// Step 5: Optional trimming at boundary (conservative — currently a no-op,
	// ready for future tuning)
	TrimToEdges(RefinedMask, SeedMask, EdgeMap, GenW, GenH, 2);
}

} // namespace SegmentationLocal

// ============================================================================
// FSegmentedActor
// ============================================================================

FString FSegmentedActor::GetBase64PNG() const
{
	if (IsolationPNGData.Num() == 0) return FString();
	return FBase64::Encode(IsolationPNGData);
}

// ============================================================================
// FSegmentationCapture
// ============================================================================

FSegmentationCapture::FSegmentationCapture()
{
}

FSegmentationCapture::~FSegmentationCapture()
{
	ClearCapture();
}

void FSegmentationCapture::ClearCapture()
{
	// During editor shutdown UObjects may already be destroyed.
	// Use ::IsValid() before touching any UObject to avoid index assertions.
	if (SegmentationTexture && ::IsValid(SegmentationTexture) && SegmentationTexture->IsRooted())
	{
		SegmentationTexture->RemoveFromRoot();
	}
	SegmentationTexture = nullptr;
	SegmentationPNGData.Empty();
	SegWidth = 0;
	SegHeight = 0;
	SegmentCount = 0;

	for (FSegmentedActor& Seg : SegmentedActors)
	{
		if (Seg.IsolationTexture && ::IsValid(Seg.IsolationTexture) && Seg.IsolationTexture->IsRooted())
		{
			Seg.IsolationTexture->RemoveFromRoot();
		}
		Seg.IsolationTexture = nullptr;
	}
	SegmentedActors.Empty();
	MaskRegions.Empty();
	MaskOwnership.Empty();
}

FString FSegmentationCapture::GetBase64PNG() const
{
	if (SegmentationPNGData.Num() == 0) return FString();
	return FBase64::Encode(SegmentationPNGData);
}

FColor FSegmentationCapture::IndexToColor(int32 Index)
{
	if (Index == 0)
	{
		return FColor(0, 0, 0, 255);
	}

	const float GoldenRatio = 0.618033988749895f;
	float Hue = FMath::Frac(Index * GoldenRatio) * 360.0f;
	float Saturation = 0.85f;
	float Value = 0.95f;

	FLinearColor LC = FLinearColor::MakeFromHSV8(
		static_cast<uint8>(Hue / 360.0f * 255.0f),
		static_cast<uint8>(Saturation * 255.0f),
		static_cast<uint8>(Value * 255.0f));

	return LC.ToFColor(true);
}

bool FSegmentationCapture::CaptureSegmentation(const FString& Mode, int32 DesiredWidth, int32 DesiredHeight)
{
	ClearCapture();

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();

	if (!ActiveLevelViewport.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Segmentation: No active level viewport found"));
		return false;
	}

	FEditorViewportClient& ViewportClient = ActiveLevelViewport->GetLevelViewportClient();
	FSceneViewport* SceneViewport = ActiveLevelViewport->GetSceneViewport().Get();
	if (!SceneViewport)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Segmentation: Could not get scene viewport"));
		return false;
	}

	FIntPoint ViewportSize = SceneViewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return false;
	}

	int32 CaptureWidth = (DesiredWidth > 0) ? DesiredWidth : ViewportSize.X;
	int32 CaptureHeight = (DesiredHeight > 0) ? DesiredHeight : ViewportSize.Y;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Segmentation: No editor world available"));
		return false;
	}

	FVector CameraLocation = ViewportClient.GetViewLocation();
	FRotator CameraRotation = ViewportClient.GetViewRotation();
	float CameraFOV = ViewportClient.ViewFOV;

	// ---- Collect visible actors ----
	TArray<AActor*> VisibleActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsHidden() || !Actor->GetRootComponent())
		{
			continue;
		}

		if (Actor->IsA(ALandscapeProxy::StaticClass()))
		{
			continue;
		}

		TArray<UPrimitiveComponent*> PrimComps;
		Actor->GetComponents<UPrimitiveComponent>(PrimComps);
		bool bHasVisible = false;
		for (UPrimitiveComponent* Comp : PrimComps)
		{
			if (Comp && Comp->IsVisible())
			{
				bHasVisible = true;
				break;
			}
		}

		if (bHasVisible)
		{
			VisibleActors.Add(Actor);
		}
	}

	if (VisibleActors.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Segmentation: No visible actors found"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen Segmentation: Found %d visible actors to segment"), VisibleActors.Num());

	// ---- Create render target ----
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RT->RenderTargetFormat = RTF_RGBA8;
	RT->ClearColor = FLinearColor::Black;
	RT->bAutoGenerateMips = false;
	RT->InitAutoFormat(CaptureWidth, CaptureHeight);
	RT->UpdateResourceImmediate(true);

	// ---- Spawn temporary capture actor ----
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	AActor* CaptureActor = World->SpawnActor<AActor>(AActor::StaticClass(),
		FTransform(CameraRotation, CameraLocation), SpawnParams);
	if (!CaptureActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Segmentation: Failed to spawn capture actor"));
		return false;
	}

	USceneComponent* RootComp = NewObject<USceneComponent>(CaptureActor, TEXT("Root"));
	CaptureActor->SetRootComponent(RootComp);
	RootComp->RegisterComponent();
	RootComp->SetWorldLocationAndRotation(CameraLocation, CameraRotation);

	USceneCaptureComponent2D* CaptureComp = NewObject<USceneCaptureComponent2D>(CaptureActor, TEXT("SegCapture"));
	CaptureComp->SetupAttachment(RootComp);
	CaptureComp->RegisterComponent();
	CaptureComp->SetWorldLocationAndRotation(CameraLocation, CameraRotation);

	CaptureComp->TextureTarget = RT;
	CaptureComp->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
	CaptureComp->bCaptureEveryFrame = false;
	CaptureComp->bCaptureOnMovement = false;
	CaptureComp->FOVAngle = CameraFOV;
	CaptureComp->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;

	// ---- Render each actor individually and build the mask ----
	const int32 PixelCount = CaptureWidth * CaptureHeight;
	TArray<FColor> MaskPixels;
	MaskPixels.SetNumZeroed(PixelCount);

	MaskOwnership.SetNumZeroed(PixelCount);

	int32 ActorsRendered = 0;
	for (int32 ActorIdx = 0; ActorIdx < VisibleActors.Num(); ++ActorIdx)
	{
		AActor* Actor = VisibleActors[ActorIdx];
		FColor ActorColor = IndexToColor(ActorIdx + 1);

		CaptureComp->ShowOnlyActors.Empty();
		CaptureComp->ShowOnlyActors.Add(Actor);

		CaptureComp->CaptureScene();
		FlushRenderingCommands();

		FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
		if (!RTResource) continue;

		TArray<FColor> Pixels;
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(false);
		RTResource->ReadPixels(Pixels, ReadFlags);

		if (Pixels.Num() != PixelCount) continue;

		bool bHadPixels = false;
		for (int32 i = 0; i < PixelCount; ++i)
		{
			const FColor& P = Pixels[i];
			if (P.R > 8 || P.G > 8 || P.B > 8)
			{
				MaskPixels[i] = ActorColor;
				MaskOwnership[i] = ActorIdx + 1;
				bHadPixels = true;
			}
		}

		if (bHadPixels)
		{
			ActorsRendered++;

			FMaskRegion Region;
			Region.ActorName = Actor->GetActorLabel().IsEmpty()
				? Actor->GetName() : Actor->GetActorLabel();
			Region.MaskColor = ActorColor;
			Region.ActorIndex = ActorIdx + 1;
			MaskRegions.Add(MoveTemp(Region));
		}
	}

	CaptureActor->Destroy();

	SegmentCount = ActorsRendered;
	SegWidth = CaptureWidth;
	SegHeight = CaptureHeight;

	UE_LOG(LogTemp, Log, TEXT("ViewGen Segmentation: Captured %dx%d mask with %d segments from %d actors"),
		SegWidth, SegHeight, SegmentCount, VisibleActors.Num());

	FViewportCapture::EncodeToPNG(MaskPixels, SegWidth, SegHeight, SegmentationPNGData);

	SegmentationTexture = CreateTextureFromPixels(MaskPixels, SegWidth, SegHeight);
	if (SegmentationTexture)
	{
		SegmentationTexture->AddToRoot();
	}

	return SegmentationTexture != nullptr;
}

bool FSegmentationCapture::SegmentGeneratedImage(UTexture2D* GeneratedTexture)
{
	if (!GeneratedTexture || MaskRegions.Num() == 0 || MaskOwnership.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Segmentation: No mask data or no generated texture"));
		return false;
	}

	// Clean up previous segmented actor images
	for (FSegmentedActor& Seg : SegmentedActors)
	{
		if (Seg.IsolationTexture && ::IsValid(Seg.IsolationTexture) && Seg.IsolationTexture->IsRooted())
		{
			Seg.IsolationTexture->RemoveFromRoot();
		}
		Seg.IsolationTexture = nullptr;
	}
	SegmentedActors.Empty();

	// Read pixels from the generated texture
	TArray<FColor> GenPixels;
	int32 GenWidth = 0, GenHeight = 0;
	if (!ReadTexturePixels(GeneratedTexture, GenPixels, GenWidth, GenHeight))
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Segmentation: Failed to read generated image pixels"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen Segmentation: Segmenting generated image %dx%d (mask %dx%d)"),
		GenWidth, GenHeight, SegWidth, SegHeight);

	const int32 GenPixelCount = GenWidth * GenHeight;

	// For each actor region, refine the mask against the generated image then extract
	for (const FMaskRegion& Region : MaskRegions)
	{
		// Edge-aware refinement: uses the coarse viewport mask as a seed, then
		// tightens the boundary based on edge detection and background colour analysis
		// in the actual generated image.
		TArray<uint8> RefinedMask;
		SegmentationLocal::RefineMask(
			RefinedMask, MaskOwnership, SegWidth, SegHeight,
			Region.ActorIndex, GenPixels, GenWidth, GenHeight);

		// Build isolation image: white background, generated pixels where mask is 1
		TArray<FColor> IsolationPixels;
		IsolationPixels.SetNum(GenPixelCount);

		bool bHadPixels = false;
		for (int32 i = 0; i < GenPixelCount; ++i)
		{
			if (RefinedMask[i])
			{
				IsolationPixels[i] = GenPixels[i];
				bHadPixels = true;
			}
			else
			{
				IsolationPixels[i] = FColor(255, 255, 255, 255);
			}
		}

		if (bHadPixels)
		{
			FSegmentedActor SegActor;
			SegActor.ActorName = Region.ActorName;
			SegActor.MaskColor = Region.MaskColor;

			FViewportCapture::EncodeToPNG(IsolationPixels, GenWidth, GenHeight, SegActor.IsolationPNGData);

			SegActor.IsolationTexture = CreateTextureFromPixels(IsolationPixels, GenWidth, GenHeight);
			if (SegActor.IsolationTexture)
			{
				SegActor.IsolationTexture->AddToRoot();
			}

			SegmentedActors.Add(MoveTemp(SegActor));

			UE_LOG(LogTemp, Log, TEXT("ViewGen Segmentation: Isolated '%s' from generated image (%d bytes PNG)"),
				*SegmentedActors.Last().ActorName, SegmentedActors.Last().IsolationPNGData.Num());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen Segmentation: Produced %d isolation images from generated result"),
		SegmentedActors.Num());

	return SegmentedActors.Num() > 0;
}

bool FSegmentationCapture::ReadTexturePixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		return false;
	}

	OutWidth = Texture->GetSizeX();
	OutHeight = Texture->GetSizeY();

	FTexturePlatformData* PlatformData = Texture->GetPlatformData();
	FByteBulkData& BulkData = PlatformData->Mips[0].BulkData;

	const int32 ExpectedSize = OutWidth * OutHeight * sizeof(FColor);
	if (BulkData.GetBulkDataSize() < ExpectedSize)
	{
		return false;
	}

	const void* RawData = BulkData.LockReadOnly();
	if (!RawData)
	{
		return false;
	}

	OutPixels.SetNum(OutWidth * OutHeight);
	FMemory::Memcpy(OutPixels.GetData(), RawData, ExpectedSize);
	BulkData.Unlock();

	return true;
}

UTexture2D* FSegmentationCapture::CreateTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
	if (Pixels.Num() != Width * Height || Width <= 0 || Height <= 0)
	{
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture) return nullptr;

	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->SRGB = true;
	Texture->NeverStream = true;

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	Texture->AddToRoot(); // Prevent GC — caller must RemoveFromRoot when done

	return Texture;
}
