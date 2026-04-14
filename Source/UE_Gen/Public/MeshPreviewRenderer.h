// Copyright UE_Gen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UStaticMesh;

/**
 * Renders a UStaticMesh to a UTexture2D via an offscreen SceneCaptureComponent2D.
 * Used for inline 3D mesh previews on graph nodes.
 *
 * Supports interactive orbit (yaw/pitch), zoom (distance), and pan (target offset).
 * Call Render() after changing any camera parameter to update the output texture.
 *
 * All UObject references use TWeakObjectPtr to prevent stale pointer crashes
 * when GC compaction runs during editor save operations.
 */
class FMeshPreviewRenderer
{
public:
	FMeshPreviewRenderer();
	~FMeshPreviewRenderer();

	/** Set the mesh to preview. Triggers an automatic render. */
	void SetMesh(UStaticMesh* InMesh);

	/** Get the current mesh */
	UStaticMesh* GetMesh() const { return PreviewMesh.Get(); }

	/** Render (or re-render) the mesh to the output texture using current camera state. */
	void Render();

	/** Get the rendered texture (null if nothing rendered yet) */
	UTexture2D* GetPreviewTexture() const { return PreviewTexture; }

	/** Get a Slate brush wrapping the preview texture */
	TSharedPtr<FSlateBrush> GetPreviewBrush() const { return PreviewBrush; }

	/** Refresh the brush's TObjectPtr handle — must be called before Slate painting
	 *  to ensure the internal packed index is current after GC compaction. */
	void RefreshBrushHandle()
	{
		if (PreviewTexture && ::IsValid(PreviewTexture) && PreviewBrush.IsValid())
		{
			PreviewBrush->SetResourceObject(PreviewTexture);
		}
	}

	/** Whether a valid preview is available */
	bool HasPreview() const { return PreviewTexture != nullptr; }

	// ---- Camera Controls ----

	/** Orbit the camera. DeltaYaw/DeltaPitch in degrees. */
	void Orbit(float DeltaYaw, float DeltaPitch);

	/** Zoom in/out. Positive = zoom in (decrease distance). */
	void Zoom(float DeltaDistance);

	/** Pan the target. DeltaX/DeltaY in world units relative to camera right/up. */
	void Pan(float DeltaX, float DeltaY);

	/** Reset camera to default orbit view */
	void ResetCamera();

	/** Mark as needing a re-render on next request */
	void MarkDirty() { bDirty = true; }
	bool IsDirty() const { return bDirty; }

	/** Render only if dirty, returns true if a render occurred */
	bool RenderIfDirty();

	// ---- Camera State ----

	float OrbitYaw = -30.0f;      // Degrees
	float OrbitPitch = -20.0f;    // Degrees (negative = looking down)
	float OrbitDistance = 0.0f;   // Auto-computed from mesh bounds if 0
	FVector PanOffset = FVector::ZeroVector;

	// ---- Render Settings ----
	int32 RenderWidth = 256;
	int32 RenderHeight = 256;

private:
	/** Create the preview world, lighting, and capture component */
	void SetupPreviewScene();

	/** Tear down the preview scene */
	void TeardownPreviewScene();

	/** Compute a good default orbit distance from mesh bounds */
	float ComputeDefaultDistance() const;

	/** Read render target pixels and create a UTexture2D */
	UTexture2D* CreateTextureFromRT(UTextureRenderTarget2D* RT);

	// Preview scene objects — weak pointers to survive GC compaction
	TWeakObjectPtr<AActor> PreviewActor;
	TWeakObjectPtr<class UStaticMeshComponent> MeshComponent;
	TWeakObjectPtr<class USceneCaptureComponent2D> CaptureComponent;
	TWeakObjectPtr<UTextureRenderTarget2D> RenderTarget;

	// Mesh — weak pointer since we don't own the imported mesh
	TWeakObjectPtr<UStaticMesh> PreviewMesh;

	// Output — rooted to prevent GC (we own these)
	UTexture2D* PreviewTexture = nullptr;
	TSharedPtr<FSlateBrush> PreviewBrush;

	bool bSceneReady = false;
	bool bDirty = true;
};
