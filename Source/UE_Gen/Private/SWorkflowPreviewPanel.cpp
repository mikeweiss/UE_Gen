// Copyright UE_Gen. All Rights Reserved.

#include "SWorkflowPreviewPanel.h"
#include "GenAISettings.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "SWorkflowPreviewPanel"

// ============================================================================
// Node Colour Palette
// ============================================================================

FLinearColor SWorkflowPreviewPanel::GetNodeColor(const FString& ClassType)
{
	if (ClassType.Contains(TEXT("Checkpoint")) || ClassType.Contains(TEXT("UNET")) || ClassType.Contains(TEXT("CLIP")) || ClassType.Contains(TEXT("VAELoader")))
		return FLinearColor(0.25f, 0.42f, 0.55f);  // blue – model loaders
	if (ClassType.Contains(TEXT("CLIPTextEncode")))
		return FLinearColor(0.30f, 0.55f, 0.30f);  // green – text encoding
	if (ClassType.Contains(TEXT("KSampler")) || ClassType.Contains(TEXT("Xlabs")))
		return FLinearColor(0.60f, 0.35f, 0.55f);  // purple – sampling
	if (ClassType.Contains(TEXT("VAE")))
		return FLinearColor(0.55f, 0.50f, 0.25f);  // gold – VAE ops
	if (ClassType.Contains(TEXT("ControlNet")) || ClassType.Contains(TEXT("FluxControl")))
		return FLinearColor(0.55f, 0.35f, 0.20f);  // orange – control
	if (ClassType.Contains(TEXT("LoRA")) || ClassType.Contains(TEXT("Lora")))
		return FLinearColor(0.50f, 0.40f, 0.60f);  // lavender – LoRA
	if (ClassType.Contains(TEXT("LoadImage")))
		return FLinearColor(0.35f, 0.50f, 0.45f);  // teal – images
	if (ClassType.Contains(TEXT("SaveImage")))
		return FLinearColor(0.20f, 0.55f, 0.20f);  // bright green – output
	if (ClassType.Contains(TEXT("EmptyLatent")))
		return FLinearColor(0.45f, 0.45f, 0.45f);  // grey – latent
	if (ClassType.Contains(TEXT("LatentUpscale")))
		return FLinearColor(0.50f, 0.50f, 0.30f);  // olive – upscale
	if (ClassType.Contains(TEXT("Gemini")) || ClassType.Contains(TEXT("NanoBanana")))
		return FLinearColor(0.20f, 0.45f, 0.60f);  // sky blue – Gemini
	if (ClassType.Contains(TEXT("Kling")))
		return FLinearColor(0.55f, 0.25f, 0.35f);  // magenta – Kling
	if (ClassType.Contains(TEXT("ImageBatch")))
		return FLinearColor(0.40f, 0.50f, 0.50f);  // cyan-grey – batch
	return FLinearColor(0.40f, 0.40f, 0.40f);      // default grey
}

// ============================================================================
// Construction
// ============================================================================

void SWorkflowPreviewPanel::Construct(const FArguments& InArgs)
{
	RefreshGraph();
}

// ============================================================================
// Graph Building Helpers
// ============================================================================

FWorkflowNode& SWorkflowPreviewPanel::AddNode(const FString& Id, const FString& ClassType,
	const FString& DisplayName, FLinearColor Color)
{
	int32 Idx = Nodes.Num();
	FWorkflowNode& Node = Nodes.AddDefaulted_GetRef();
	Node.Id = Id;
	Node.ClassType = ClassType;
	Node.DisplayName = DisplayName;
	Node.Color = Color;
	NodeIndexMap.Add(Id, Idx);
	return Node;
}

/** Helper: truncate long strings for display */
static FString TruncateForDisplay(const FString& Value, int32 MaxChars = 22)
{
	if (Value.Len() <= MaxChars) return Value;
	return Value.Left(MaxChars - 3) + TEXT("...");
}

// ============================================================================
// RefreshGraph — build lightweight graph from current settings
// ============================================================================

void SWorkflowPreviewPanel::RefreshGraph()
{
	Nodes.Empty();
	NodeIndexMap.Empty();

	const UGenAISettings* Settings = UGenAISettings::Get();
	if (!Settings) return;

	switch (Settings->GenerationMode)
	{
	case EGenMode::Img2Img:       BuildImg2ImgGraph(); break;
	case EGenMode::DepthAndPrompt: BuildDepthOnlyGraph(); break;
	case EGenMode::PromptOnly:    BuildTxt2ImgGraph(); break;
	case EGenMode::Gemini:        BuildGeminiGraph(); break;
	case EGenMode::Kling:         BuildKlingGraph(); break;
	}

	LayoutNodes();
}

// ============================================================================
// Txt2Img Graph
// ============================================================================

void SWorkflowPreviewPanel::BuildTxt2ImgGraph()
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	const bool bFluxMode = Settings->bUseFluxControlNet;

	FString ModelSourceId, ClipSourceId, VAESourceId;
	int32 NextNodeId = 20;

	if (bFluxMode)
	{
		auto& UnetNode = AddNode(TEXT("20"), TEXT("UNETLoader"), TEXT("UNETLoader"), GetNodeColor(TEXT("UNETLoader")));
		UnetNode.DisplayParams.Add(TPair<FString, FString>(TEXT("model"), TruncateForDisplay(Settings->FluxModelName)));
		UnetNode.DisplayParams.Add(TPair<FString, FString>(TEXT("dtype"), Settings->FluxWeightDtype));

		auto& ClipNode = AddNode(TEXT("21"), TEXT("DualCLIPLoader"), TEXT("DualCLIPLoader"), GetNodeColor(TEXT("CLIPLoader")));
		ClipNode.DisplayParams.Add(TPair<FString, FString>(TEXT("clip1"), TruncateForDisplay(Settings->FluxCLIPName1)));
		ClipNode.DisplayParams.Add(TPair<FString, FString>(TEXT("clip2"), TruncateForDisplay(Settings->FluxCLIPName2)));

		auto& VaeNode = AddNode(TEXT("22"), TEXT("VAELoader"), TEXT("VAELoader"), GetNodeColor(TEXT("VAELoader")));
		VaeNode.DisplayParams.Add(TPair<FString, FString>(TEXT("vae"), TruncateForDisplay(Settings->FluxVAEName)));

		ModelSourceId = TEXT("20");
		ClipSourceId = TEXT("21");
		VAESourceId = TEXT("22");
		NextNodeId = 23;
	}
	else
	{
		auto& CkptNode = AddNode(TEXT("1"), TEXT("CheckpointLoaderSimple"), TEXT("Load Checkpoint"), GetNodeColor(TEXT("Checkpoint")));
		CkptNode.DisplayParams.Add(TPair<FString, FString>(TEXT("ckpt"), TruncateForDisplay(Settings->CheckpointName)));

		ModelSourceId = TEXT("1");
		ClipSourceId = TEXT("1");
		VAESourceId = TEXT("1");
	}

	// LoRA chain
	FString LoRAFinalModel = AddLoRANodes(ModelSourceId, ClipSourceId, NextNodeId);
	if (!LoRAFinalModel.IsEmpty())
	{
		ModelSourceId = LoRAFinalModel;
		ClipSourceId = LoRAFinalModel;
	}

	// Positive CLIP
	auto& PosClip = AddNode(TEXT("2"), TEXT("CLIPTextEncode"), TEXT("CLIP (Positive)"), GetNodeColor(TEXT("CLIPTextEncode")));
	PosClip.DisplayParams.Add(TPair<FString, FString>(TEXT("prompt"), TruncateForDisplay(Settings->DefaultPrompt, 30)));
	PosClip.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("clip"), TPair<FString, int32>(ClipSourceId, bFluxMode ? 0 : 1)));

	// Negative CLIP
	auto& NegClip = AddNode(TEXT("3"), TEXT("CLIPTextEncode"), TEXT("CLIP (Negative)"), GetNodeColor(TEXT("CLIPTextEncode")));
	NegClip.DisplayParams.Add(TPair<FString, FString>(TEXT("prompt"), TruncateForDisplay(Settings->DefaultNegativePrompt, 30)));
	NegClip.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("clip"), TPair<FString, int32>(ClipSourceId, bFluxMode ? 0 : 1)));

	// Empty Latent
	auto& EmptyLatent = AddNode(TEXT("4"), TEXT("EmptyLatentImage"), TEXT("Empty Latent"), GetNodeColor(TEXT("EmptyLatent")));
	EmptyLatent.DisplayParams.Add(TPair<FString, FString>(TEXT("size"), FString::Printf(TEXT("%dx%d"), Settings->OutputWidth, Settings->OutputHeight)));

	// KSampler
	auto& Sampler = AddNode(TEXT("5"), TEXT("KSampler"), TEXT("KSampler"), GetNodeColor(TEXT("KSampler")));
	Sampler.DisplayParams.Add(TPair<FString, FString>(TEXT("steps"), FString::FromInt(Settings->Steps)));
	Sampler.DisplayParams.Add(TPair<FString, FString>(TEXT("cfg"), FString::SanitizeFloat(Settings->CFGScale)));
	Sampler.DisplayParams.Add(TPair<FString, FString>(TEXT("sampler"), Settings->SamplerName));
	Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("model"), TPair<FString, int32>(ModelSourceId, 0)));
	Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("positive"), TPair<FString, int32>(TEXT("2"), 0)));
	Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("negative"), TPair<FString, int32>(TEXT("3"), 0)));
	Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("latent"), TPair<FString, int32>(TEXT("4"), 0)));

	// VAEDecode
	auto& VaeDec = AddNode(TEXT("6"), TEXT("VAEDecode"), TEXT("VAE Decode"), GetNodeColor(TEXT("VAE")));
	VaeDec.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("samples"), TPair<FString, int32>(TEXT("5"), 0)));
	VaeDec.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("vae"), TPair<FString, int32>(VAESourceId, bFluxMode ? 0 : 2)));

	// SaveImage
	auto& Save = AddNode(TEXT("7"), TEXT("SaveImage"), TEXT("Save Image"), GetNodeColor(TEXT("SaveImage")));
	Save.DisplayParams.Add(TPair<FString, FString>(TEXT("prefix"), TEXT("UE_Gen")));
	Save.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("images"), TPair<FString, int32>(TEXT("6"), 0)));

	// Hi-Res Fix
	if (Settings->bEnableHiResFix)
	{
		AddHiResFixNodes(TEXT("5"), ModelSourceId, TEXT("2"), TEXT("3"), VAESourceId, bFluxMode ? 0 : 2, NextNodeId);
	}
}

// ============================================================================
// Img2Img Graph
// ============================================================================

void SWorkflowPreviewPanel::BuildImg2ImgGraph()
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	const bool bFluxMode = Settings->bUseFluxControlNet;

	FString ModelSourceId, ClipSourceId, VAESourceId;
	int32 NextNodeId = 20;

	if (bFluxMode)
	{
		auto& UnetNode = AddNode(TEXT("20"), TEXT("UNETLoader"), TEXT("UNETLoader"), GetNodeColor(TEXT("UNETLoader")));
		UnetNode.DisplayParams.Add(TPair<FString, FString>(TEXT("model"), TruncateForDisplay(Settings->FluxModelName)));

		auto& ClipNode = AddNode(TEXT("21"), TEXT("DualCLIPLoader"), TEXT("DualCLIPLoader"), GetNodeColor(TEXT("CLIPLoader")));
		ClipNode.DisplayParams.Add(TPair<FString, FString>(TEXT("clip1"), TruncateForDisplay(Settings->FluxCLIPName1)));

		auto& VaeNode = AddNode(TEXT("22"), TEXT("VAELoader"), TEXT("VAELoader"), GetNodeColor(TEXT("VAELoader")));
		VaeNode.DisplayParams.Add(TPair<FString, FString>(TEXT("vae"), TruncateForDisplay(Settings->FluxVAEName)));

		ModelSourceId = TEXT("20");
		ClipSourceId = TEXT("21");
		VAESourceId = TEXT("22");
		NextNodeId = 23;
	}
	else
	{
		auto& CkptNode = AddNode(TEXT("1"), TEXT("CheckpointLoaderSimple"), TEXT("Load Checkpoint"), GetNodeColor(TEXT("Checkpoint")));
		CkptNode.DisplayParams.Add(TPair<FString, FString>(TEXT("ckpt"), TruncateForDisplay(Settings->CheckpointName)));

		ModelSourceId = TEXT("1");
		ClipSourceId = TEXT("1");
		VAESourceId = TEXT("1");
	}

	// LoRA chain
	FString LoRAFinal = AddLoRANodes(ModelSourceId, ClipSourceId, NextNodeId);
	if (!LoRAFinal.IsEmpty())
	{
		ModelSourceId = LoRAFinal;
		ClipSourceId = LoRAFinal;
	}

	// CLIP Positive
	auto& PosClip = AddNode(TEXT("2"), TEXT("CLIPTextEncode"), TEXT("CLIP (Positive)"), GetNodeColor(TEXT("CLIPTextEncode")));
	PosClip.DisplayParams.Add(TPair<FString, FString>(TEXT("prompt"), TruncateForDisplay(Settings->DefaultPrompt, 30)));
	PosClip.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("clip"), TPair<FString, int32>(ClipSourceId, bFluxMode ? 0 : 1)));

	// CLIP Negative
	auto& NegClip = AddNode(TEXT("3"), TEXT("CLIPTextEncode"), TEXT("CLIP (Negative)"), GetNodeColor(TEXT("CLIPTextEncode")));
	NegClip.DisplayParams.Add(TPair<FString, FString>(TEXT("prompt"), TruncateForDisplay(Settings->DefaultNegativePrompt, 30)));
	NegClip.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("clip"), TPair<FString, int32>(ClipSourceId, bFluxMode ? 0 : 1)));

	// LoadImage (viewport)
	auto& LoadImg = AddNode(TEXT("4"), TEXT("LoadImage"), TEXT("Load Viewport"), GetNodeColor(TEXT("LoadImage")));
	LoadImg.DisplayParams.Add(TPair<FString, FString>(TEXT("image"), TEXT("viewport capture")));

	// VAEEncode
	auto& VaeEnc = AddNode(TEXT("5"), TEXT("VAEEncode"), TEXT("VAE Encode"), GetNodeColor(TEXT("VAE")));
	VaeEnc.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("pixels"), TPair<FString, int32>(TEXT("4"), 0)));
	VaeEnc.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("vae"), TPair<FString, int32>(VAESourceId, bFluxMode ? 0 : 2)));

	// ControlNet nodes (if depth enabled)
	FString PositiveCondId = TEXT("2");
	bool bUsedFluxSampler = false;

	if (Settings->bEnableDepthControlNet)
	{
		auto& DepthLoad = AddNode(FString::FromInt(NextNodeId), TEXT("LoadImage"), TEXT("Load Depth"), GetNodeColor(TEXT("LoadImage")));
		DepthLoad.DisplayParams.Add(TPair<FString, FString>(TEXT("image"), TEXT("depth map")));
		FString DepthLoadId = FString::FromInt(NextNodeId++);

		if (bFluxMode)
		{
			FString FluxCNLoaderId = FString::FromInt(NextNodeId++);
			auto& CnLoader = AddNode(FluxCNLoaderId, TEXT("LoadFluxControlNet"), TEXT("Load Flux CN"), GetNodeColor(TEXT("ControlNet")));
			CnLoader.DisplayParams.Add(TPair<FString, FString>(TEXT("model"), TruncateForDisplay(Settings->FluxModelName)));
			CnLoader.DisplayParams.Add(TPair<FString, FString>(TEXT("cn_path"), TruncateForDisplay(Settings->ControlNetModel)));

			FString FluxCNApplyId = FString::FromInt(NextNodeId++);
			auto& CnApply = AddNode(FluxCNApplyId, TEXT("ApplyFluxControlNet"), TEXT("Apply Flux CN"), GetNodeColor(TEXT("ControlNet")));
			CnApply.DisplayParams.Add(TPair<FString, FString>(TEXT("strength"), FString::SanitizeFloat(Settings->ControlNetWeight)));
			CnApply.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("controlnet"), TPair<FString, int32>(FluxCNLoaderId, 0)));
			CnApply.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("image"), TPair<FString, int32>(DepthLoadId, 0)));

			// XlabsSampler
			auto& XSampler = AddNode(TEXT("6"), TEXT("XlabsSampler"), TEXT("Xlabs Sampler"), GetNodeColor(TEXT("KSampler")));
			XSampler.DisplayParams.Add(TPair<FString, FString>(TEXT("steps"), FString::FromInt(Settings->Steps)));
			XSampler.DisplayParams.Add(TPair<FString, FString>(TEXT("denoise"), FString::SanitizeFloat(Settings->DenoisingStrength)));
			XSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("model"), TPair<FString, int32>(ModelSourceId, 0)));
			XSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("cond+"), TPair<FString, int32>(TEXT("2"), 0)));
			XSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("cond-"), TPair<FString, int32>(TEXT("3"), 0)));
			XSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("latent"), TPair<FString, int32>(TEXT("5"), 0)));
			XSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("cn_cond"), TPair<FString, int32>(FluxCNApplyId, 0)));

			bUsedFluxSampler = true;
		}
		else
		{
			FString CNLoaderId = FString::FromInt(NextNodeId++);
			auto& CnLoader = AddNode(CNLoaderId, TEXT("ControlNetLoader"), TEXT("Load ControlNet"), GetNodeColor(TEXT("ControlNet")));
			CnLoader.DisplayParams.Add(TPair<FString, FString>(TEXT("model"), TruncateForDisplay(Settings->ControlNetModel)));

			FString CNApplyId = FString::FromInt(NextNodeId++);
			auto& CnApply = AddNode(CNApplyId, TEXT("ControlNetApply"), TEXT("Apply ControlNet"), GetNodeColor(TEXT("ControlNet")));
			CnApply.DisplayParams.Add(TPair<FString, FString>(TEXT("strength"), FString::SanitizeFloat(Settings->ControlNetWeight)));
			CnApply.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("cond"), TPair<FString, int32>(TEXT("2"), 0)));
			CnApply.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("cn"), TPair<FString, int32>(CNLoaderId, 0)));
			CnApply.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("image"), TPair<FString, int32>(DepthLoadId, 0)));

			PositiveCondId = CNApplyId;
		}
	}

	// KSampler (if not Flux sampler)
	if (!bUsedFluxSampler)
	{
		auto& Sampler = AddNode(TEXT("6"), TEXT("KSampler"), TEXT("KSampler"), GetNodeColor(TEXT("KSampler")));
		Sampler.DisplayParams.Add(TPair<FString, FString>(TEXT("steps"), FString::FromInt(Settings->Steps)));
		Sampler.DisplayParams.Add(TPair<FString, FString>(TEXT("cfg"), FString::SanitizeFloat(Settings->CFGScale)));
		Sampler.DisplayParams.Add(TPair<FString, FString>(TEXT("denoise"), FString::SanitizeFloat(Settings->DenoisingStrength)));
		Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("model"), TPair<FString, int32>(ModelSourceId, 0)));
		Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("positive"), TPair<FString, int32>(PositiveCondId, 0)));
		Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("negative"), TPair<FString, int32>(TEXT("3"), 0)));
		Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("latent"), TPair<FString, int32>(TEXT("5"), 0)));
	}

	// VAEDecode
	auto& VaeDec = AddNode(TEXT("7"), TEXT("VAEDecode"), TEXT("VAE Decode"), GetNodeColor(TEXT("VAE")));
	VaeDec.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("samples"), TPair<FString, int32>(TEXT("6"), 0)));
	VaeDec.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("vae"), TPair<FString, int32>(VAESourceId, bFluxMode ? 0 : 2)));

	// SaveImage
	auto& Save = AddNode(TEXT("8"), TEXT("SaveImage"), TEXT("Save Image"), GetNodeColor(TEXT("SaveImage")));
	Save.DisplayParams.Add(TPair<FString, FString>(TEXT("prefix"), TEXT("UE_Gen")));
	Save.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("images"), TPair<FString, int32>(TEXT("7"), 0)));

	if (Settings->bEnableHiResFix)
	{
		AddHiResFixNodes(TEXT("6"), ModelSourceId, PositiveCondId, TEXT("3"), VAESourceId, bFluxMode ? 0 : 2, NextNodeId);
	}
}

// ============================================================================
// Depth + Prompt Graph
// ============================================================================

void SWorkflowPreviewPanel::BuildDepthOnlyGraph()
{
	// Very similar to Txt2Img but with ControlNet depth instead of viewport img2img
	const UGenAISettings* Settings = UGenAISettings::Get();
	const bool bFluxMode = Settings->bUseFluxControlNet;

	FString ModelSourceId, ClipSourceId, VAESourceId;
	int32 NextNodeId = 20;

	if (bFluxMode)
	{
		AddNode(TEXT("20"), TEXT("UNETLoader"), TEXT("UNETLoader"), GetNodeColor(TEXT("UNETLoader")))
			.DisplayParams.Add(TPair<FString, FString>(TEXT("model"), TruncateForDisplay(Settings->FluxModelName)));
		AddNode(TEXT("21"), TEXT("DualCLIPLoader"), TEXT("DualCLIPLoader"), GetNodeColor(TEXT("CLIPLoader")));
		AddNode(TEXT("22"), TEXT("VAELoader"), TEXT("VAELoader"), GetNodeColor(TEXT("VAELoader")));
		ModelSourceId = TEXT("20"); ClipSourceId = TEXT("21"); VAESourceId = TEXT("22");
		NextNodeId = 23;
	}
	else
	{
		auto& CkptNode = AddNode(TEXT("1"), TEXT("CheckpointLoaderSimple"), TEXT("Load Checkpoint"), GetNodeColor(TEXT("Checkpoint")));
		CkptNode.DisplayParams.Add(TPair<FString, FString>(TEXT("ckpt"), TruncateForDisplay(Settings->CheckpointName)));
		ModelSourceId = TEXT("1"); ClipSourceId = TEXT("1"); VAESourceId = TEXT("1");
	}

	FString LoRAFinal = AddLoRANodes(ModelSourceId, ClipSourceId, NextNodeId);
	if (!LoRAFinal.IsEmpty()) { ModelSourceId = LoRAFinal; ClipSourceId = LoRAFinal; }

	auto& PosClip = AddNode(TEXT("2"), TEXT("CLIPTextEncode"), TEXT("CLIP (Positive)"), GetNodeColor(TEXT("CLIPTextEncode")));
	PosClip.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("clip"), TPair<FString, int32>(ClipSourceId, bFluxMode ? 0 : 1)));

	auto& NegClip = AddNode(TEXT("3"), TEXT("CLIPTextEncode"), TEXT("CLIP (Negative)"), GetNodeColor(TEXT("CLIPTextEncode")));
	NegClip.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("clip"), TPair<FString, int32>(ClipSourceId, bFluxMode ? 0 : 1)));

	auto& EmptyLatent = AddNode(TEXT("4"), TEXT("EmptyLatentImage"), TEXT("Empty Latent"), GetNodeColor(TEXT("EmptyLatent")));
	EmptyLatent.DisplayParams.Add(TPair<FString, FString>(TEXT("size"), FString::Printf(TEXT("%dx%d"), Settings->OutputWidth, Settings->OutputHeight)));

	// Depth image + ControlNet
	auto& DepthLoad = AddNode(FString::FromInt(NextNodeId), TEXT("LoadImage"), TEXT("Load Depth"), GetNodeColor(TEXT("LoadImage")));
	DepthLoad.DisplayParams.Add(TPair<FString, FString>(TEXT("image"), TEXT("depth map")));
	FString DepthLoadId = FString::FromInt(NextNodeId++);

	FString PositiveCondId = TEXT("2");

	if (bFluxMode)
	{
		FString FluxCNLoaderId = FString::FromInt(NextNodeId++);
		auto& CnLoad = AddNode(FluxCNLoaderId, TEXT("LoadFluxControlNet"), TEXT("Load Flux CN"), GetNodeColor(TEXT("ControlNet")));
		CnLoad.DisplayParams.Add(TPair<FString, FString>(TEXT("cn_path"), TruncateForDisplay(Settings->ControlNetModel)));

		FString FluxCNApplyId = FString::FromInt(NextNodeId++);
		auto& CnApply = AddNode(FluxCNApplyId, TEXT("ApplyFluxControlNet"), TEXT("Apply Flux CN"), GetNodeColor(TEXT("ControlNet")));
		CnApply.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("controlnet"), TPair<FString, int32>(FluxCNLoaderId, 0)));
		CnApply.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("image"), TPair<FString, int32>(DepthLoadId, 0)));

		auto& XSampler = AddNode(TEXT("5"), TEXT("XlabsSampler"), TEXT("Xlabs Sampler"), GetNodeColor(TEXT("KSampler")));
		XSampler.DisplayParams.Add(TPair<FString, FString>(TEXT("steps"), FString::FromInt(Settings->Steps)));
		XSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("model"), TPair<FString, int32>(ModelSourceId, 0)));
		XSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("cond+"), TPair<FString, int32>(TEXT("2"), 0)));
		XSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("cond-"), TPair<FString, int32>(TEXT("3"), 0)));
		XSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("latent"), TPair<FString, int32>(TEXT("4"), 0)));
		XSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("cn_cond"), TPair<FString, int32>(FluxCNApplyId, 0)));
	}
	else
	{
		FString CNLoaderId = FString::FromInt(NextNodeId++);
		auto& CnLoad = AddNode(CNLoaderId, TEXT("ControlNetLoader"), TEXT("Load ControlNet"), GetNodeColor(TEXT("ControlNet")));
		CnLoad.DisplayParams.Add(TPair<FString, FString>(TEXT("model"), TruncateForDisplay(Settings->ControlNetModel)));

		FString CNApplyId = FString::FromInt(NextNodeId++);
		auto& CnApply = AddNode(CNApplyId, TEXT("ControlNetApply"), TEXT("Apply ControlNet"), GetNodeColor(TEXT("ControlNet")));
		CnApply.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("cond"), TPair<FString, int32>(TEXT("2"), 0)));
		CnApply.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("cn"), TPair<FString, int32>(CNLoaderId, 0)));
		CnApply.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("image"), TPair<FString, int32>(DepthLoadId, 0)));

		PositiveCondId = CNApplyId;

		auto& Sampler = AddNode(TEXT("5"), TEXT("KSampler"), TEXT("KSampler"), GetNodeColor(TEXT("KSampler")));
		Sampler.DisplayParams.Add(TPair<FString, FString>(TEXT("steps"), FString::FromInt(Settings->Steps)));
		Sampler.DisplayParams.Add(TPair<FString, FString>(TEXT("cfg"), FString::SanitizeFloat(Settings->CFGScale)));
		Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("model"), TPair<FString, int32>(ModelSourceId, 0)));
		Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("positive"), TPair<FString, int32>(PositiveCondId, 0)));
		Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("negative"), TPair<FString, int32>(TEXT("3"), 0)));
		Sampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("latent"), TPair<FString, int32>(TEXT("4"), 0)));
	}

	auto& VaeDec = AddNode(TEXT("6"), TEXT("VAEDecode"), TEXT("VAE Decode"), GetNodeColor(TEXT("VAE")));
	VaeDec.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("samples"), TPair<FString, int32>(TEXT("5"), 0)));
	VaeDec.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("vae"), TPair<FString, int32>(VAESourceId, bFluxMode ? 0 : 2)));

	auto& Save = AddNode(TEXT("7"), TEXT("SaveImage"), TEXT("Save Image"), GetNodeColor(TEXT("SaveImage")));
	Save.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("images"), TPair<FString, int32>(TEXT("6"), 0)));

	if (Settings->bEnableHiResFix)
	{
		AddHiResFixNodes(TEXT("5"), ModelSourceId, PositiveCondId, TEXT("3"), VAESourceId, bFluxMode ? 0 : 2, NextNodeId);
	}
}

// ============================================================================
// Gemini Graph
// ============================================================================

void SWorkflowPreviewPanel::BuildGeminiGraph()
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	int32 NextNodeId = 1;

	// Optional viewport and depth image loaders
	FString ViewportLoadId, DepthLoadId;

	// Assume viewport is available (user captured it)
	ViewportLoadId = FString::FromInt(NextNodeId++);
	auto& ViewportLoad = AddNode(ViewportLoadId, TEXT("LoadImage"), TEXT("Load Viewport"), GetNodeColor(TEXT("LoadImage")));
	ViewportLoad.DisplayParams.Add(TPair<FString, FString>(TEXT("image"), TEXT("viewport capture")));

	DepthLoadId = FString::FromInt(NextNodeId++);
	auto& DepthLoad = AddNode(DepthLoadId, TEXT("LoadImage"), TEXT("Load Depth"), GetNodeColor(TEXT("LoadImage")));
	DepthLoad.DisplayParams.Add(TPair<FString, FString>(TEXT("image"), TEXT("depth map")));

	// ImageBatch
	FString BatchId = FString::FromInt(NextNodeId++);
	auto& Batch = AddNode(BatchId, TEXT("ImageBatch"), TEXT("Image Batch"), GetNodeColor(TEXT("ImageBatch")));
	Batch.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("image1"), TPair<FString, int32>(ViewportLoadId, 0)));
	Batch.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("image2"), TPair<FString, int32>(DepthLoadId, 0)));

	// GeminiNanoBanana2
	FString GeminiId = FString::FromInt(NextNodeId++);
	auto& GeminiNode = AddNode(GeminiId, TEXT("GeminiNanoBanana2"), TEXT("Gemini NanoBanana2"), GetNodeColor(TEXT("Gemini")));
	GeminiNode.DisplayParams.Add(TPair<FString, FString>(TEXT("model"), Settings->GeminiModelName));
	GeminiNode.DisplayParams.Add(TPair<FString, FString>(TEXT("aspect"), Settings->GeminiAspectRatio));
	GeminiNode.DisplayParams.Add(TPair<FString, FString>(TEXT("resolution"), Settings->GeminiResolution));
	GeminiNode.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("images"), TPair<FString, int32>(BatchId, 0)));

	// SaveImage
	FString SaveId = FString::FromInt(NextNodeId++);
	auto& Save = AddNode(SaveId, TEXT("SaveImage"), TEXT("Save Image"), GetNodeColor(TEXT("SaveImage")));
	Save.DisplayParams.Add(TPair<FString, FString>(TEXT("prefix"), TEXT("UE_Gen_Gemini")));
	Save.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("images"), TPair<FString, int32>(GeminiId, 0)));
}

// ============================================================================
// Kling Graph
// ============================================================================

void SWorkflowPreviewPanel::BuildKlingGraph()
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	int32 NextNodeId = 1;

	// Optional viewport
	FString ViewportLoadId = FString::FromInt(NextNodeId++);
	auto& ViewportLoad = AddNode(ViewportLoadId, TEXT("LoadImage"), TEXT("Load Viewport"), GetNodeColor(TEXT("LoadImage")));
	ViewportLoad.DisplayParams.Add(TPair<FString, FString>(TEXT("image"), TEXT("viewport capture")));

	// KlingImageGenerationNode
	FString KlingId = FString::FromInt(NextNodeId++);
	auto& KlingNode = AddNode(KlingId, TEXT("KlingImageGenerationNode"), TEXT("Kling Image Gen"), GetNodeColor(TEXT("Kling")));
	KlingNode.DisplayParams.Add(TPair<FString, FString>(TEXT("model"), Settings->KlingModelName));
	KlingNode.DisplayParams.Add(TPair<FString, FString>(TEXT("aspect"), Settings->KlingAspectRatio));
	KlingNode.DisplayParams.Add(TPair<FString, FString>(TEXT("type"), Settings->KlingImageType));
	KlingNode.DisplayParams.Add(TPair<FString, FString>(TEXT("fidelity"), FString::SanitizeFloat(Settings->KlingImageFidelity)));
	KlingNode.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("image"), TPair<FString, int32>(ViewportLoadId, 0)));

	// SaveImage
	FString SaveId = FString::FromInt(NextNodeId++);
	auto& Save = AddNode(SaveId, TEXT("SaveImage"), TEXT("Save Image"), GetNodeColor(TEXT("SaveImage")));
	Save.DisplayParams.Add(TPair<FString, FString>(TEXT("prefix"), TEXT("UE_Gen_Kling")));
	Save.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("images"), TPair<FString, int32>(KlingId, 0)));
}

// ============================================================================
// LoRA Chain Helper
// ============================================================================

FString SWorkflowPreviewPanel::AddLoRANodes(const FString& ModelSourceId, const FString& ClipSourceId, int32& NextNodeId)
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	FString PrevModelId = ModelSourceId;
	FString LastLoRAId;

	for (const FLoRAEntry& LoRA : Settings->LoRAModels)
	{
		if (!LoRA.bEnabled || LoRA.PathOrIdentifier.IsEmpty()) continue;

		FString LoRAId = FString::FromInt(NextNodeId++);
		auto& LoRANode = AddNode(LoRAId, TEXT("LoraLoader"), TEXT("LoRA"), GetNodeColor(TEXT("LoRA")));
		LoRANode.DisplayParams.Add(TPair<FString, FString>(TEXT("lora"), TruncateForDisplay(LoRA.Name)));
		LoRANode.DisplayParams.Add(TPair<FString, FString>(TEXT("weight"), FString::SanitizeFloat(LoRA.Weight)));
		LoRANode.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("model"), TPair<FString, int32>(PrevModelId, 0)));

		PrevModelId = LoRAId;
		LastLoRAId = LoRAId;
	}

	return LastLoRAId;
}

// ============================================================================
// Hi-Res Fix Helper
// ============================================================================

void SWorkflowPreviewPanel::AddHiResFixNodes(const FString& KSamplerId, const FString& ModelId,
	const FString& PosCondId, const FString& NegCondId,
	const FString& VAESourceId, int32 VAEOutputIndex, int32& NextNodeId)
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	FString UpscaleId = FString::FromInt(NextNodeId++);
	auto& Upscale = AddNode(UpscaleId, TEXT("LatentUpscale"), TEXT("Latent Upscale"), GetNodeColor(TEXT("LatentUpscale")));
	Upscale.DisplayParams.Add(TPair<FString, FString>(TEXT("factor"), FString::SanitizeFloat(Settings->HiResUpscaleFactor)));
	Upscale.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("samples"), TPair<FString, int32>(KSamplerId, 0)));

	FString HiResSamplerId = FString::FromInt(NextNodeId++);
	auto& HiResSampler = AddNode(HiResSamplerId, TEXT("KSampler"), TEXT("KSampler (Hi-Res)"), GetNodeColor(TEXT("KSampler")));
	HiResSampler.DisplayParams.Add(TPair<FString, FString>(TEXT("steps"), FString::FromInt(Settings->HiResSteps)));
	HiResSampler.DisplayParams.Add(TPair<FString, FString>(TEXT("denoise"), FString::SanitizeFloat(Settings->HiResDenoise)));
	HiResSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("model"), TPair<FString, int32>(ModelId, 0)));
	HiResSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("positive"), TPair<FString, int32>(PosCondId, 0)));
	HiResSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("negative"), TPair<FString, int32>(NegCondId, 0)));
	HiResSampler.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("latent"), TPair<FString, int32>(UpscaleId, 0)));

	FString HiResDecodeId = FString::FromInt(NextNodeId++);
	auto& HiResDecode = AddNode(HiResDecodeId, TEXT("VAEDecode"), TEXT("VAE Decode (HR)"), GetNodeColor(TEXT("VAE")));
	HiResDecode.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("samples"), TPair<FString, int32>(HiResSamplerId, 0)));
	HiResDecode.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("vae"), TPair<FString, int32>(VAESourceId, VAEOutputIndex)));

	FString HiResSaveId = FString::FromInt(NextNodeId++);
	auto& HiResSave = AddNode(HiResSaveId, TEXT("SaveImage"), TEXT("Save Image (HR)"), GetNodeColor(TEXT("SaveImage")));
	HiResSave.InputLinks.Add(TPair<FString, TPair<FString, int32>>(TEXT("images"), TPair<FString, int32>(HiResDecodeId, 0)));
}

// ============================================================================
// Auto-Layout (left-to-right by topological depth)
// ============================================================================

TMap<FString, int32> SWorkflowPreviewPanel::ComputeNodeDepths() const
{
	TMap<FString, int32> Depths;

	// Initialize all to 0
	for (const FWorkflowNode& Node : Nodes)
	{
		Depths.Add(Node.Id, 0);
	}

	// Iteratively compute longest path from sources
	bool bChanged = true;
	int32 SafetyCounter = 0;
	while (bChanged && SafetyCounter++ < 100)
	{
		bChanged = false;
		for (const FWorkflowNode& Node : Nodes)
		{
			for (const auto& Link : Node.InputLinks)
			{
				const FString& SourceId = Link.Value.Key;
				if (Depths.Contains(SourceId))
				{
					int32 NewDepth = Depths[SourceId] + 1;
					if (NewDepth > Depths[Node.Id])
					{
						Depths[Node.Id] = NewDepth;
						bChanged = true;
					}
				}
			}
		}
	}

	return Depths;
}

void SWorkflowPreviewPanel::LayoutNodes()
{
	if (Nodes.Num() == 0) return;

	TMap<FString, int32> Depths = ComputeNodeDepths();

	// Find max depth
	int32 MaxDepth = 0;
	for (const auto& Pair : Depths)
	{
		MaxDepth = FMath::Max(MaxDepth, Pair.Value);
	}

	// Compute node sizes based on content
	const float BaseWidth = 180.0f;
	const float HeaderHeight = 24.0f;
	const float ParamLineHeight = 16.0f;
	const float Padding = 8.0f;

	for (FWorkflowNode& Node : Nodes)
	{
		float Height = HeaderHeight + Padding * 2;
		Height += Node.DisplayParams.Num() * ParamLineHeight;
		Height += Node.InputLinks.Num() * ParamLineHeight;
		Height = FMath::Max(Height, 50.0f);
		Node.Size = FVector2D(BaseWidth, Height);
	}

	// Group nodes by depth column
	TArray<TArray<int32>> Columns;
	Columns.SetNum(MaxDepth + 1);

	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		int32 Depth = Depths.FindRef(Nodes[i].Id);
		Columns[Depth].Add(i);
	}

	// Position nodes
	const float ColSpacing = 240.0f;
	const float RowSpacing = 20.0f;

	for (int32 Col = 0; Col <= MaxDepth; ++Col)
	{
		float X = Col * ColSpacing;
		float TotalHeight = 0.0f;

		for (int32 Idx : Columns[Col])
		{
			TotalHeight += Nodes[Idx].Size.Y + RowSpacing;
		}
		TotalHeight -= RowSpacing;

		float Y = -TotalHeight * 0.5f;

		for (int32 Idx : Columns[Col])
		{
			Nodes[Idx].Position = FVector2D(X, Y);
			Y += Nodes[Idx].Size.Y + RowSpacing;
		}
	}
}

// ============================================================================
// Coordinate Transform
// ============================================================================

FVector2D SWorkflowPreviewPanel::GraphToLocal(FVector2D GraphPos) const
{
	return (GraphPos + ViewOffset) * ZoomLevel + FVector2D(100.0f, 200.0f);
}

// ============================================================================
// OnPaint — draw nodes and connections
// ============================================================================

int32 SWorkflowPreviewPanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Background
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		FCoreStyle::Get().GetBrush("GenericWhiteBox"),
		ESlateDrawEffect::None,
		FLinearColor(0.05f, 0.05f, 0.06f)
	);
	LayerId++;

	// Push clipping rect so node drawing stays within this widget's bounds
	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry));

	// Draw connections first (behind nodes)
	for (const FWorkflowNode& Node : Nodes)
	{
		FVector2D NodeCenter = GraphToLocal(Node.Position + FVector2D(0.0f, Node.Size.Y * 0.5f));

		for (int32 LinkIdx = 0; LinkIdx < Node.InputLinks.Num(); ++LinkIdx)
		{
			const auto& Link = Node.InputLinks[LinkIdx];
			const FString& SourceId = Link.Value.Key;

			const int32* SourceIdxPtr = NodeIndexMap.Find(SourceId);
			if (!SourceIdxPtr) continue;

			const FWorkflowNode& SourceNode = Nodes[*SourceIdxPtr];

			// Output from right side of source, input to left side of target
			FVector2D Start = GraphToLocal(SourceNode.Position + FVector2D(SourceNode.Size.X, SourceNode.Size.Y * 0.5f));
			FVector2D End = GraphToLocal(Node.Position + FVector2D(0.0f, Node.Size.Y * 0.3f + LinkIdx * 12.0f));

			DrawConnection(Start, End, FLinearColor(0.6f, 0.6f, 0.6f, 0.5f), AllottedGeometry, OutDrawElements, LayerId);
		}
	}
	LayerId++;

	// Draw nodes
	for (const FWorkflowNode& Node : Nodes)
	{
		DrawNode(Node, AllottedGeometry, OutDrawElements, LayerId);
	}
	LayerId += 2;

	// Pop clipping rect
	OutDrawElements.PopClip();

	return LayerId;
}

// ============================================================================
// DrawNode
// ============================================================================

void SWorkflowPreviewPanel::DrawNode(const FWorkflowNode& Node, const FGeometry& Geom,
	FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FVector2D Pos = GraphToLocal(Node.Position);
	const FVector2D Size = Node.Size * ZoomLevel;

	// Node body (dark box)
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		Geom.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)),
		FCoreStyle::Get().GetBrush("GenericWhiteBox"),
		ESlateDrawEffect::None,
		FLinearColor(0.12f, 0.12f, 0.14f)
	);

	// Header bar (colored)
	const float HeaderH = 22.0f * ZoomLevel;
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId + 1,
		Geom.ToPaintGeometry(FVector2D(Size.X, HeaderH), FSlateLayoutTransform(Pos)),
		FCoreStyle::Get().GetBrush("GenericWhiteBox"),
		ESlateDrawEffect::None,
		Node.Color
	);

	// Node title text
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", static_cast<int32>(FMath::Max(7.0f, 9.0f * ZoomLevel)));
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId + 1,
		Geom.ToPaintGeometry(FVector2D(Size.X - 8.0f, HeaderH), FSlateLayoutTransform(Pos + FVector2D(4.0f, 2.0f * ZoomLevel))),
		Node.DisplayName,
		TitleFont,
		ESlateDrawEffect::None,
		FLinearColor::White
	);

	// Parameters text
	const FSlateFontInfo ParamFont = FCoreStyle::GetDefaultFontStyle("Regular", static_cast<int32>(FMath::Max(6.0f, 7.5f * ZoomLevel)));
	float YOffset = HeaderH + 4.0f * ZoomLevel;

	for (const auto& Param : Node.DisplayParams)
	{
		FString ParamText = FString::Printf(TEXT("%s: %s"), *Param.Key, *Param.Value);
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 1,
			Geom.ToPaintGeometry(FVector2D(Size.X - 8.0f, 14.0f * ZoomLevel), FSlateLayoutTransform(Pos + FVector2D(6.0f, YOffset))),
			ParamText,
			ParamFont,
			ESlateDrawEffect::None,
			FLinearColor(0.7f, 0.7f, 0.7f)
		);
		YOffset += 14.0f * ZoomLevel;
	}

	// Input pin labels
	for (const auto& Link : Node.InputLinks)
	{
		FString PinText = FString::Printf(TEXT("\x2190 %s"), *Link.Key);  // ← arrow
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 1,
			Geom.ToPaintGeometry(FVector2D(Size.X - 8.0f, 14.0f * ZoomLevel), FSlateLayoutTransform(Pos + FVector2D(6.0f, YOffset))),
			PinText,
			ParamFont,
			ESlateDrawEffect::None,
			FLinearColor(0.5f, 0.5f, 0.55f)
		);
		YOffset += 14.0f * ZoomLevel;
	}

	// Thin border
	// Top
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
		Geom.ToPaintGeometry(FVector2D(Size.X, 1.0f), FSlateLayoutTransform(Pos)),
		FCoreStyle::Get().GetBrush("GenericWhiteBox"), ESlateDrawEffect::None,
		FLinearColor(Node.Color.R * 0.8f, Node.Color.G * 0.8f, Node.Color.B * 0.8f, 0.6f));
	// Bottom
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
		Geom.ToPaintGeometry(FVector2D(Size.X, 1.0f), FSlateLayoutTransform(Pos + FVector2D(0, Size.Y - 1.0f))),
		FCoreStyle::Get().GetBrush("GenericWhiteBox"), ESlateDrawEffect::None,
		FLinearColor(0.2f, 0.2f, 0.22f, 0.6f));
	// Left
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
		Geom.ToPaintGeometry(FVector2D(1.0f, Size.Y), FSlateLayoutTransform(Pos)),
		FCoreStyle::Get().GetBrush("GenericWhiteBox"), ESlateDrawEffect::None,
		FLinearColor(0.2f, 0.2f, 0.22f, 0.6f));
	// Right
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
		Geom.ToPaintGeometry(FVector2D(1.0f, Size.Y), FSlateLayoutTransform(Pos + FVector2D(Size.X - 1.0f, 0))),
		FCoreStyle::Get().GetBrush("GenericWhiteBox"), ESlateDrawEffect::None,
		FLinearColor(0.2f, 0.2f, 0.22f, 0.6f));
}

// ============================================================================
// DrawConnection — cubic bezier curve
// ============================================================================

void SWorkflowPreviewPanel::DrawConnection(FVector2D Start, FVector2D End, FLinearColor Color,
	const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	// Draw as a series of line segments approximating a bezier
	const int32 NumSegments = 16;
	TArray<FVector2D> Points;
	Points.Reserve(NumSegments + 1);

	float TangentLength = FMath::Abs(End.X - Start.X) * 0.5f;
	TangentLength = FMath::Max(TangentLength, 40.0f);

	FVector2D P0 = Start;
	FVector2D P1 = Start + FVector2D(TangentLength, 0.0f);
	FVector2D P2 = End - FVector2D(TangentLength, 0.0f);
	FVector2D P3 = End;

	for (int32 i = 0; i <= NumSegments; ++i)
	{
		float t = static_cast<float>(i) / NumSegments;
		float u = 1.0f - t;
		FVector2D Pt = u * u * u * P0 + 3.0f * u * u * t * P1 + 3.0f * u * t * t * P2 + t * t * t * P3;
		Points.Add(Pt);
	}

	// Draw as connected thin box segments (more compatible than MakeCustomVerts)
	const float Thickness = FMath::Max(1.5f * ZoomLevel, 1.0f);

	for (int32 i = 0; i < Points.Num() - 1; ++i)
	{
		FVector2D A = Points[i];
		FVector2D B = Points[i + 1];
		FVector2D Dir = B - A;
		float Length = Dir.Size();
		if (Length < 0.1f) continue;

		// Approximate curved line with axis-aligned thin boxes
		FVector2D MinPt(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
		FVector2D MaxPt(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
		FVector2D SegSize(FMath::Max(MaxPt.X - MinPt.X, Thickness), FMath::Max(MaxPt.Y - MinPt.Y, Thickness));

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			Geom.ToPaintGeometry(SegSize, FSlateLayoutTransform(MinPt)),
			FCoreStyle::Get().GetBrush("GenericWhiteBox"),
			ESlateDrawEffect::None,
			Color
		);
	}
}

// ============================================================================
// Mouse Interaction (Pan + Zoom)
// ============================================================================

FReply SWorkflowPreviewPanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton || MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		bIsPanning = true;
		LastMousePos = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SWorkflowPreviewPanel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsPanning)
	{
		bIsPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SWorkflowPreviewPanel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsPanning)
	{
		FVector2D CurrentPos = MouseEvent.GetScreenSpacePosition();
		FVector2D Delta = (CurrentPos - LastMousePos) / ZoomLevel;
		ViewOffset += Delta;
		LastMousePos = CurrentPos;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SWorkflowPreviewPanel::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	float Delta = MouseEvent.GetWheelDelta();
	float NewZoom = FMath::Clamp(ZoomLevel + Delta * 0.1f, 0.3f, 3.0f);
	ZoomLevel = NewZoom;
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
