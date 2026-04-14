// Copyright UE_Gen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

/**
 * Captures a segmentation mask from the active editor viewport.
 * Each visible actor receives a unique colour, producing a flat colour-coded
 * mask image that downstream segmentation or SAM nodes can use to isolate individual
 * elements in the scene.
 *
 * After a generated image is received, SegmentGeneratedImage() uses the stored mask
 * regions to cut the generated result into per-actor isolation images on solid white
 * backgrounds — so the isolated parts match the AI-generated output, not the raw viewport.
 *
 * Technique: uses SceneCaptureComponent2D with PRM_UseShowOnlyList to render each
 * actor individually against a black background, then composites non-black pixels
 * into a single mask using golden-ratio hue distribution for visually distinct colours.
 */

/** Data for a single segmented actor */
struct FSegmentedActor
{
	/** Display name of the actor */
	FString ActorName;

	/** The unique mask colour assigned to this actor */
	FColor MaskColor;

	/** The actor's region cut from the generated image, on a white background */
	TObjectPtr<UTexture2D> IsolationTexture;

	/** PNG-encoded bytes of the isolation image (white background) */
	TArray<uint8> IsolationPNGData;

	/** Base64-encoded PNG for upload payloads */
	FString GetBase64PNG() const;
};

class FSegmentationCapture
{
public:
	FSegmentationCapture();
	~FSegmentationCapture();

	/**
	 * Capture a segmentation mask from the active viewport.
	 * Stores the mask regions internally for later use with SegmentGeneratedImage().
	 * @param Mode          "Actor ID" (currently the only supported mode)
	 * @param DesiredWidth  Target width (0 = match viewport)
	 * @param DesiredHeight Target height (0 = match viewport)
	 * @return True if capture succeeded
	 */
	bool CaptureSegmentation(const FString& Mode = TEXT("Actor ID"),
		int32 DesiredWidth = 0, int32 DesiredHeight = 0);

	/**
	 * Segment a generated image using the stored mask regions.
	 * For each actor, the viewport mask is used as a coarse seed, then refined
	 * against the generated image using edge detection and background-colour
	 * analysis to produce a tight, pixel-accurate boundary.
	 * Each actor's pixels are extracted onto a solid white background.
	 * Must be called after a successful CaptureSegmentation().
	 * @param GeneratedTexture The AI-generated result image
	 * @return True if segmentation produced at least one actor
	 */
	bool SegmentGeneratedImage(UTexture2D* GeneratedTexture);

	/** Get the composite segmentation mask as a UTexture2D */
	UTexture2D* GetSegmentationTexture() const { return SegmentationTexture; }

	/** Get segmentation data as PNG-encoded bytes */
	const TArray<uint8>& GetPNGData() const { return SegmentationPNGData; }

	/** Get base64-encoded PNG for JSON payloads */
	FString GetBase64PNG() const;

	/** Dimensions of the captured segmentation mask */
	int32 GetWidth() const { return SegWidth; }
	int32 GetHeight() const { return SegHeight; }

	bool HasCapture() const { return SegmentationTexture != nullptr; }

	/** Whether mask regions are stored and ready for SegmentGeneratedImage() */
	bool HasMaskData() const { return MaskRegions.Num() > 0; }

	void ClearCapture();

	/** Get the number of unique segments found in the last capture */
	int32 GetSegmentCount() const { return SegmentCount; }

	/** Get the array of per-actor segmented images (populated by SegmentGeneratedImage) */
	const TArray<FSegmentedActor>& GetSegmentedActors() const { return SegmentedActors; }

private:
	/**
	 * Generate a deterministic but visually distinct colour from an index.
	 * Uses a golden-ratio hue distribution for maximum separation.
	 */
	static FColor IndexToColor(int32 Index);

	/** Create a UTexture2D from colour pixels */
	UTexture2D* CreateTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height);

	/** Read pixels from a UTexture2D (locks mip 0) */
	static bool ReadTexturePixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);

	TObjectPtr<UTexture2D> SegmentationTexture;

	TArray<uint8> SegmentationPNGData;
	int32 SegWidth = 0;
	int32 SegHeight = 0;
	int32 SegmentCount = 0;

	/** Per-actor isolation images — populated by SegmentGeneratedImage() */
	TArray<FSegmentedActor> SegmentedActors;

	/** Stored mask region data: which actor index (1-based) owns each pixel (0 = background) */
	struct FMaskRegion
	{
		FString ActorName;
		FColor MaskColor;
		int32 ActorIndex; // 1-based index used in MaskOwnership
	};
	TArray<FMaskRegion> MaskRegions;

	/** Per-pixel ownership: index into MaskRegions (0 = background, 1+ = actor) */
	TArray<int32> MaskOwnership;
};
