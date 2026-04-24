// Copyright ViewGen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

/** Result of a single Meshy Image-to-3D task */
struct FMeshyTaskResult
{
	FString TaskId;
	FString Status; // PENDING, IN_PROGRESS, SUCCEEDED, FAILED
	int32 Progress = 0;

	/** Download URLs for the generated model (populated on SUCCEEDED) */
	FString GLBUrl;
	FString FBXUrl;
	FString OBJUrl;

	/** Texture URLs */
	FString BaseColorUrl;
	FString NormalUrl;

	/** Thumbnail preview URL */
	FString ThumbnailUrl;
};

DECLARE_DELEGATE_TwoParams(FOnMeshyTaskCreated, bool /*bSuccess*/, const FString& /*TaskId or Error*/);
DECLARE_DELEGATE_OneParam(FOnMeshyTaskProgress, const FMeshyTaskResult& /*Result*/);
DECLARE_DELEGATE_OneParam(FOnMeshyTaskComplete, const FMeshyTaskResult& /*Result*/);
DECLARE_DELEGATE_OneParam(FOnMeshyModelDownloaded, const FString& /*LocalFilePath*/);
DECLARE_DELEGATE_OneParam(FOnMeshyTaskListReceived, const TArray<FMeshyTaskResult>& /*Tasks*/);

/**
 * HTTP client for the Meshy Image-to-3D API.
 *
 * Flow:
 * 1. CreateImageTo3DTask() — uploads an image (as base64 data URI) and creates a task
 * 2. PollTask() — polls for completion status
 * 3. DownloadModel() — downloads the GLB model once SUCCEEDED
 */
class FMeshyApiClient
{
public:
	FMeshyApiClient();
	~FMeshyApiClient();

	/**
	 * Create an Image-to-3D task from a base64 PNG image.
	 * @param Base64PNG  Base64-encoded PNG image data
	 * @param bEnablePBR Enable PBR texture generation
	 * @param bRemesh    Enable topology remeshing
	 */
	void CreateImageTo3DTask(const FString& Base64PNG, bool bEnablePBR = true, bool bRemesh = true);

	/** Poll the status of an in-progress task */
	void PollTask(const FString& TaskId);

	/** Download the GLB model from a completed task to a local file */
	void DownloadModel(const FString& GLBUrl, const FString& LocalSavePath);

	/** List recent Image-to-3D tasks from the Meshy API.
	 *  Returns up to Limit tasks, sorted newest first. */
	void ListRecentTasks(int32 Limit = 5);

	/** Cancel polling */
	void CancelPolling();

	/** Callbacks */
	FOnMeshyTaskCreated OnTaskCreated;
	FOnMeshyTaskProgress OnTaskProgress;
	FOnMeshyTaskComplete OnTaskComplete;
	FOnMeshyModelDownloaded OnModelDownloaded;
	FOnMeshyTaskListReceived OnTaskListReceived;

	/** Whether any request is in progress */
	bool IsRequestInProgress() const { return bRequestInProgress; }

private:
	void OnCreateTaskResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void OnPollTaskResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void OnDownloadResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void OnListTasksResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	FMeshyTaskResult ParseTaskResult(TSharedPtr<FJsonObject> JsonObj) const;

	FString CurrentTaskId;
	FString CurrentDownloadPath;
	bool bRequestInProgress = false;
};
