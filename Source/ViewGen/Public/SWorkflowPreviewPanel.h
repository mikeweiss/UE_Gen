// Copyright ViewGen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "GenAISettings.h"

/**
 * Lightweight representation of a single node in the workflow graph.
 */
struct FWorkflowNode
{
	FString Id;
	FString ClassType;
	FString DisplayName;
	FLinearColor Color;

	/** Position computed by auto-layout */
	FVector2D Position = FVector2D::ZeroVector;

	/** Size computed from content */
	FVector2D Size = FVector2D(180.0f, 60.0f);

	/** Key input values to display (parameter name -> display value) */
	TArray<TPair<FString, FString>> DisplayParams;

	/** Input connections: input pin name -> (source node id, source output index) */
	TArray<TPair<FString, TPair<FString, int32>>> InputLinks;
};

/**
 * Slate widget that renders a read-only node graph preview of the ComfyUI
 * workflow that would be generated with the current settings.
 *
 * Uses OnPaint to draw node boxes and bezier connection lines.
 * Supports pan (right-drag) and zoom (scroll wheel).
 */
class SWorkflowPreviewPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWorkflowPreviewPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuild the graph from current UGenAISettings */
	void RefreshGraph();

	// SWidget overrides
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	// ---- Graph Building ----

	/** Build nodes for img2img mode */
	void BuildImg2ImgGraph();

	/** Build nodes for depth+prompt mode */
	void BuildDepthOnlyGraph();

	/** Build nodes for txt2img mode */
	void BuildTxt2ImgGraph();

	/** Build nodes for Gemini mode */
	void BuildGeminiGraph();

	/** Build nodes for Kling mode */
	void BuildKlingGraph();

	/** Add LoRA loader chain nodes and return the final model/clip source node ID */
	FString AddLoRANodes(const FString& ModelSourceId, const FString& ClipSourceId, int32& NextNodeId);

	/** Add Hi-Res Fix nodes if enabled */
	void AddHiResFixNodes(const FString& KSamplerId, const FString& ModelId,
		const FString& PosCondId, const FString& NegCondId,
		const FString& VAESourceId, int32 VAEOutputIndex, int32& NextNodeId);

	/** Common helper: add a node to the graph */
	FWorkflowNode& AddNode(const FString& Id, const FString& ClassType,
		const FString& DisplayName, FLinearColor Color);

	// ---- Layout ----

	/** Simple left-to-right auto-layout based on topological sort */
	void LayoutNodes();

	/** Compute topological depth (longest path from sources) for each node */
	TMap<FString, int32> ComputeNodeDepths() const;

	// ---- Drawing Helpers ----

	/** Draw a single node box */
	void DrawNode(const FWorkflowNode& Node, const FGeometry& Geom,
		FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Draw a bezier curve between two points */
	void DrawConnection(FVector2D Start, FVector2D End, FLinearColor Color,
		const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Transform graph coordinates to widget local coordinates */
	FVector2D GraphToLocal(FVector2D GraphPos) const;

	// ---- Color Palette ----
	static FLinearColor GetNodeColor(const FString& ClassType);

	// ---- Graph Data ----
	TArray<FWorkflowNode> Nodes;
	TMap<FString, int32> NodeIndexMap; // Id -> index in Nodes array

	// ---- Viewport State ----
	mutable FVector2D ViewOffset = FVector2D::ZeroVector;
	mutable float ZoomLevel = 1.0f;
	bool bIsPanning = false;
	FVector2D LastMousePos = FVector2D::ZeroVector;
};
