// Copyright UE_Gen. All Rights Reserved.

#include "MeshPreviewRenderer.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "RenderingThread.h"
#include "Editor.h"
#include "UObject/Package.h"

FMeshPreviewRenderer::FMeshPreviewRenderer()
{
	PreviewBrush = MakeShareable(new FSlateBrush());
	PreviewBrush->DrawAs = ESlateBrushDrawType::NoDrawType;
}

FMeshPreviewRenderer::~FMeshPreviewRenderer()
{
	TeardownPreviewScene();

	if (PreviewTexture && ::IsValid(PreviewTexture) && PreviewTexture->IsRooted())
	{
		PreviewTexture->RemoveFromRoot();
	}
	PreviewTexture = nullptr;
	PreviewBrush.Reset();
}

void FMeshPreviewRenderer::SetMesh(UStaticMesh* InMesh)
{
	PreviewMesh = InMesh;
	OrbitDistance = 0.0f; // Reset to auto-compute
	bDirty = true;
	RenderIfDirty();
}

void FMeshPreviewRenderer::Orbit(float DeltaYaw, float DeltaPitch)
{
	OrbitYaw += DeltaYaw;
	OrbitPitch = FMath::Clamp(OrbitPitch + DeltaPitch, -89.0f, 89.0f);
	bDirty = true;
}

void FMeshPreviewRenderer::Zoom(float DeltaDistance)
{
	float DefaultDist = ComputeDefaultDistance();
	if (OrbitDistance <= 0.0f) OrbitDistance = DefaultDist;
	OrbitDistance = FMath::Max(OrbitDistance - DeltaDistance, DefaultDist * 0.1f);
	bDirty = true;
}

void FMeshPreviewRenderer::Pan(float DeltaX, float DeltaY)
{
	FRotator CamRot(OrbitPitch, OrbitYaw, 0.0f);
	FVector Right = CamRot.RotateVector(FVector::RightVector);
	FVector Up = CamRot.RotateVector(FVector::UpVector);

	PanOffset += Right * DeltaX + Up * DeltaY;
	bDirty = true;
}

void FMeshPreviewRenderer::ResetCamera()
{
	OrbitYaw = -30.0f;
	OrbitPitch = -20.0f;
	OrbitDistance = 0.0f;
	PanOffset = FVector::ZeroVector;
	bDirty = true;
}

bool FMeshPreviewRenderer::RenderIfDirty()
{
	if (!bDirty) return false;
	Render();
	return true;
}

float FMeshPreviewRenderer::ComputeDefaultDistance() const
{
	UStaticMesh* Mesh = PreviewMesh.Get();
	if (!Mesh || !::IsValid(Mesh)) return 200.0f;

	FBoxSphereBounds Bounds = Mesh->GetBounds();
	float SphereRadius = Bounds.SphereRadius;
	if (SphereRadius <= 0.0f) SphereRadius = 100.0f;

	// Place camera so the mesh fills roughly 70% of the view
	float HalfFOVRad = FMath::DegreesToRadians(30.0f); // 60 degree FOV / 2
	float Distance = SphereRadius / FMath::Tan(HalfFOVRad);
	return Distance * 1.3f;
}

void FMeshPreviewRenderer::SetupPreviewScene()
{
	if (bSceneReady) return;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen MeshPreview: No editor world available"));
		return;
	}

	// Create render target
	UTextureRenderTarget2D* NewRT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	NewRT->RenderTargetFormat = RTF_RGBA8;
	NewRT->ClearColor = FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
	NewRT->bAutoGenerateMips = false;
	NewRT->InitAutoFormat(RenderWidth, RenderHeight);
	NewRT->UpdateResourceImmediate(true);
	NewRT->AddToRoot(); // Prevent GC — we manage lifetime ourselves
	RenderTarget = NewRT;

	// Spawn a transient actor far from the scene (won't be visible in the editor viewport)
	FVector SpawnLocation(0.0f, 0.0f, -100000.0f); // Far below the scene
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(),
		FTransform(FRotator::ZeroRotator, SpawnLocation), SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen MeshPreview: Failed to spawn preview actor"));
		return;
	}
	PreviewActor = NewActor;

	// Root component
	USceneComponent* RootComp = NewObject<USceneComponent>(NewActor, TEXT("Root"));
	NewActor->SetRootComponent(RootComp);
	RootComp->RegisterComponent();
	RootComp->SetWorldLocation(SpawnLocation);

	// Static mesh component
	UStaticMeshComponent* NewMeshComp = NewObject<UStaticMeshComponent>(NewActor, TEXT("MeshDisplay"));
	NewMeshComp->SetupAttachment(RootComp);
	NewMeshComp->RegisterComponent();
	NewMeshComp->SetWorldLocation(SpawnLocation);
	NewMeshComp->SetVisibility(true);
	NewMeshComp->SetHiddenInGame(true);
	MeshComponent = NewMeshComp;

	// Scene capture component
	USceneCaptureComponent2D* NewCapture = NewObject<USceneCaptureComponent2D>(NewActor, TEXT("SceneCapture"));
	NewCapture->SetupAttachment(RootComp);
	NewCapture->RegisterComponent();
	NewCapture->TextureTarget = NewRT;
	NewCapture->bCaptureEveryFrame = false;
	NewCapture->bCaptureOnMovement = false;
	NewCapture->FOVAngle = 60.0f;
	CaptureComponent = NewCapture;

	// Use ShowOnlyComponent to only capture the mesh — excludes the rest of the scene
	NewCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	NewCapture->ShowOnlyComponents.Add(NewMeshComp);

	// Use unlit rendering since ShowOnlyList excludes scene lights.
	// This ensures the mesh is visible with its base color/textures.
	NewCapture->ShowFlags.SetLighting(false);
	NewCapture->ShowFlags.SetAtmosphere(false);
	NewCapture->ShowFlags.SetFog(false);
	NewCapture->ShowFlags.SetGrid(false);
	NewCapture->ShowFlags.SetPostProcessing(false);
	NewCapture->ShowFlags.SetBloom(false);
	NewCapture->ShowFlags.SetEyeAdaptation(false);
	NewCapture->ShowFlags.SetMotionBlur(false);

	// Use BaseColor capture so we get material colors even without lighting
	NewCapture->CaptureSource = ESceneCaptureSource::SCS_BaseColor;

	bSceneReady = true;
}

void FMeshPreviewRenderer::TeardownPreviewScene()
{
	AActor* Actor = PreviewActor.Get();
	if (Actor && GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World)
		{
			World->DestroyActor(Actor);
		}
	}
	PreviewActor.Reset();
	MeshComponent.Reset();
	CaptureComponent.Reset();

	UTextureRenderTarget2D* RT = RenderTarget.Get();
	if (RT)
	{
		if (RT->IsRooted())
		{
			RT->RemoveFromRoot();
		}
		RT->ConditionalBeginDestroy();
	}
	RenderTarget.Reset();

	bSceneReady = false;
}

void FMeshPreviewRenderer::Render()
{
	bDirty = false;

	UStaticMesh* Mesh = PreviewMesh.Get();
	if (!Mesh || !::IsValid(Mesh))
	{
		return;
	}

	// Lazy setup
	if (!bSceneReady)
	{
		SetupPreviewScene();
		if (!bSceneReady) return;
	}

	// Validate all scene objects are still alive (GC may have collected them)
	UStaticMeshComponent* MeshComp = MeshComponent.Get();
	USceneCaptureComponent2D* Capture = CaptureComponent.Get();
	UTextureRenderTarget2D* RT = RenderTarget.Get();

	if (!MeshComp || !::IsValid(MeshComp) ||
		!Capture || !::IsValid(Capture) ||
		!RT || !::IsValid(RT))
	{
		// Scene was invalidated by GC — teardown and recreate on next render
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen MeshPreview: Scene objects invalidated by GC, will recreate"));
		bSceneReady = false;
		PreviewActor.Reset();
		MeshComponent.Reset();
		CaptureComponent.Reset();
		RenderTarget.Reset();
		bDirty = true; // Try again next frame
		return;
	}

	// Set the mesh
	MeshComp->SetStaticMesh(Mesh);

	// Center the mesh at the preview location
	FBoxSphereBounds MeshBounds = Mesh->GetBounds();
	FVector MeshCenter = MeshBounds.Origin;
	FVector BaseLocation(0.0f, 0.0f, -100000.0f);
	MeshComp->SetWorldLocation(BaseLocation - MeshCenter);

	// Compute orbit camera position
	float Distance = OrbitDistance;
	if (Distance <= 0.0f) Distance = ComputeDefaultDistance();

	FRotator OrbitRotation(OrbitPitch, OrbitYaw, 0.0f);
	FVector CameraOffset = OrbitRotation.Vector() * (-Distance);
	FVector Target = BaseLocation + PanOffset;
	FVector CameraLocation = Target + CameraOffset;

	FRotator LookAtRotation = (Target - CameraLocation).Rotation();
	Capture->SetWorldLocationAndRotation(CameraLocation, LookAtRotation);

	// Perform capture
	Capture->CaptureScene();
	FlushRenderingCommands();

	// Read pixels and create texture
	UTexture2D* NewTexture = CreateTextureFromRT(RT);
	if (NewTexture)
	{
		if (PreviewTexture && ::IsValid(PreviewTexture) && PreviewTexture->IsRooted())
		{
			PreviewTexture->RemoveFromRoot();
		}

		PreviewTexture = NewTexture;
		PreviewTexture->AddToRoot();

		PreviewBrush->SetResourceObject(PreviewTexture);
		PreviewBrush->ImageSize = FVector2D(RenderWidth, RenderHeight);
		PreviewBrush->DrawAs = ESlateBrushDrawType::Image;
	}
}

UTexture2D* FMeshPreviewRenderer::CreateTextureFromRT(UTextureRenderTarget2D* RT)
{
	if (!RT || !::IsValid(RT)) return nullptr;

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNum(RenderWidth * RenderHeight);

	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	if (!RTResource->ReadPixels(Pixels, ReadFlags))
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen MeshPreview: Failed to read render target pixels"));
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(RenderWidth, RenderHeight, PF_B8G8R8A8);
	if (!Texture) return nullptr;

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();

	Texture->UpdateResource();
	return Texture;
}
