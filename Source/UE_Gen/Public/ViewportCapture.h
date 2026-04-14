// Copyright UE_Gen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

/**
 * Captures the active editor viewport as a color image.
 * Reads pixels from the viewport's render target and stores them
 * as both raw pixel data and a UTexture2D for Slate display.
 */
class FViewportCapture
{
public:
	FViewportCapture();
	~FViewportCapture();

	/**
	 * Capture the active level editor viewport.
	 * @param DesiredWidth  Target width to resize the capture (0 = native resolution)
	 * @param DesiredHeight Target height to resize the capture (0 = native resolution)
	 * @return True if capture was successful
	 */
	bool CaptureActiveViewport(int32 DesiredWidth = 0, int32 DesiredHeight = 0);

	/** Get the captured image as a UTexture2D (for Slate brush display) */
	UTexture2D* GetCapturedTexture() const { return CapturedTexture; }

	/** Get raw pixel data as PNG-encoded bytes (for sending over HTTP) */
	const TArray<uint8>& GetPNGData() const { return PNGData; }

	/** Get raw pixel data as RGBA */
	const TArray<FColor>& GetRawPixels() const { return RawPixels; }

	/** Dimensions of the last capture */
	int32 GetWidth() const { return CaptureWidth; }
	int32 GetHeight() const { return CaptureHeight; }

	/** Whether a valid capture exists */
	bool HasCapture() const { return CapturedTexture != nullptr; }

	/** Clear the stored capture data */
	void ClearCapture();

	// ---- Camera Metadata (populated during capture) ----

	/** Whether camera metadata was successfully extracted */
	bool HasCameraData() const { return bHasCameraData; }

	/** Horizontal field of view in degrees */
	float GetCameraFOV() const { return CameraFOV; }

	/** Approximate 35mm-equivalent focal length */
	float GetFocalLength35mmEquiv() const { return FocalLength35mm; }

	/** Camera world-space location */
	FVector GetCameraLocation() const { return CameraLocation; }

	/** Camera world-space rotation */
	FRotator GetCameraRotation() const { return CameraRotation; }

	/** Viewport aspect ratio (width / height) */
	float GetAspectRatio() const { return AspectRatio; }

	/**
	 * Build a human-readable camera/lens description string suitable for
	 * prepending to a generation prompt.
	 * Example: "shot with 35mm lens, eye-level angle, 16:9 widescreen, looking slightly down"
	 */
	FString BuildCameraPromptDescription() const;

	/** Encode raw pixels to PNG */
	static bool EncodeToPNG(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutPNG);

	/** Encode raw pixels to base64 PNG string (for JSON payloads) */
	FString GetBase64PNG() const;

private:
	/** Create a UTexture2D from raw pixel data */
	UTexture2D* CreateTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height);

	TObjectPtr<UTexture2D> CapturedTexture;

	TArray<FColor> RawPixels;
	TArray<uint8> PNGData;
	int32 CaptureWidth = 0;
	int32 CaptureHeight = 0;

	// Camera metadata
	bool bHasCameraData = false;
	float CameraFOV = 90.0f;
	float FocalLength35mm = 50.0f;
	float AspectRatio = 16.0f / 9.0f;
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
};
