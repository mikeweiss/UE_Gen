// Copyright UE_Gen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"

/**
 * Extracts the depth buffer from the active editor viewport using a SceneCaptureComponent2D.
 * Produces a linearized grayscale depth map suitable for ControlNet depth input.
 *
 * Close = white (255), Far = black (0)
 */
class FDepthPassRenderer
{
public:
	FDepthPassRenderer();
	~FDepthPassRenderer();

	/**
	 * Capture the depth buffer from the active viewport.
	 * @param MaxDepthDistance  Maximum scene depth in Unreal units to map to 1.0
	 * @param DesiredWidth     Target width (0 = match viewport)
	 * @param DesiredHeight    Target height (0 = match viewport)
	 * @return True if depth capture succeeded
	 */
	bool CaptureDepth(float MaxDepthDistance = 50000.0f, int32 DesiredWidth = 0, int32 DesiredHeight = 0);

	/** Get the depth map as a grayscale UTexture2D */
	UTexture2D* GetDepthTexture() const { return DepthTexture; }

	/** Get depth data as PNG-encoded bytes */
	const TArray<uint8>& GetPNGData() const { return DepthPNGData; }

	/** Get base64 encoded PNG for JSON payloads */
	FString GetBase64PNG() const;

	/** Dimensions of the captured depth map */
	int32 GetWidth() const { return DepthWidth; }
	int32 GetHeight() const { return DepthHeight; }

	bool HasCapture() const { return DepthTexture != nullptr; }

	void ClearCapture();

	float GetMaxDepthDistance() const { return LastMaxDepthDistance; }

private:
	/**
	 * Read pixels from a render target and convert linear depth to grayscale.
	 * The render target contains linear scene depth in the R channel.
	 */
	TArray<FColor> ReadAndConvertDepthRT(UTextureRenderTarget2D* RT, float MaxDepthDistance);

	/** Create a UTexture2D from grayscale depth pixels */
	UTexture2D* CreateDepthTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height);

	TObjectPtr<UTexture2D> DepthTexture;

	TArray<uint8> DepthPNGData;
	int32 DepthWidth = 0;
	int32 DepthHeight = 0;
	float LastMaxDepthDistance = 50000.0f;
};
