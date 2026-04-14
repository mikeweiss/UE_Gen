// Copyright UE_Gen. All Rights Reserved.

#include "MeshyApiClient.h"
#include "GenAISettings.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"

static const FString MeshyBaseURL = TEXT("https://api.meshy.ai/openapi/v1");

FMeshyApiClient::FMeshyApiClient()
{
}

FMeshyApiClient::~FMeshyApiClient()
{
	CancelPolling();
}

void FMeshyApiClient::CancelPolling()
{
	bRequestInProgress = false;
	CurrentTaskId.Empty();
}

void FMeshyApiClient::CreateImageTo3DTask(const FString& Base64PNG, bool bEnablePBR, bool bRemesh)
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	if (Settings->MeshyApiKey.IsEmpty())
	{
		OnTaskCreated.ExecuteIfBound(false, TEXT("Meshy API key is not set. Configure it in Project Settings > Plugins > UE Gen."));
		return;
	}

	bRequestInProgress = true;

	// Build the image data URI
	FString ImageDataURI = FString::Printf(TEXT("data:image/png;base64,%s"), *Base64PNG);

	// Build JSON body
	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
	Body->SetStringField(TEXT("image_url"), ImageDataURI);
	Body->SetBoolField(TEXT("should_texture"), true);
	Body->SetBoolField(TEXT("enable_pbr"), bEnablePBR);
	Body->SetBoolField(TEXT("should_remesh"), bRemesh);
	Body->SetBoolField(TEXT("optimize_image_prompt"), true);

	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	FString URL = MeshyBaseURL / TEXT("image-to-3d");

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings->MeshyApiKey));
	Request->SetContentAsString(BodyStr);
	Request->SetTimeout(60.0f);

	Request->OnProcessRequestComplete().BindRaw(this, &FMeshyApiClient::OnCreateTaskResponse);
	Request->ProcessRequest();
}

void FMeshyApiClient::OnCreateTaskResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		bRequestInProgress = false;
		OnTaskCreated.ExecuteIfBound(false, TEXT("Failed to connect to Meshy API"));
		return;
	}

	int32 Code = Response->GetResponseCode();
	if (Code != 200 && Code != 201 && Code != 202)
	{
		bRequestInProgress = false;
		FString ErrorMsg = FString::Printf(TEXT("Meshy API returned HTTP %d: %s"),
			Code, *Response->GetContentAsString().Left(500));
		OnTaskCreated.ExecuteIfBound(false, ErrorMsg);
		return;
	}

	TSharedPtr<FJsonObject> JsonResp;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonResp) || !JsonResp.IsValid())
	{
		bRequestInProgress = false;
		OnTaskCreated.ExecuteIfBound(false, TEXT("Failed to parse Meshy response"));
		return;
	}

	// The response may be just {"result": "task_id"} or a full task object with "id"
	FString TaskId;
	if (!JsonResp->TryGetStringField(TEXT("result"), TaskId))
	{
		JsonResp->TryGetStringField(TEXT("id"), TaskId);
	}

	if (TaskId.IsEmpty())
	{
		bRequestInProgress = false;
		OnTaskCreated.ExecuteIfBound(false, TEXT("Meshy response missing task ID"));
		return;
	}

	CurrentTaskId = TaskId;
	UE_LOG(LogTemp, Log, TEXT("UE_Gen Meshy: Task created: %s"), *TaskId);
	OnTaskCreated.ExecuteIfBound(true, TaskId);
}

void FMeshyApiClient::PollTask(const FString& TaskId)
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	FString URL = MeshyBaseURL / TEXT("image-to-3d") / TaskId;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings->MeshyApiKey));
	Request->SetTimeout(15.0f);

	Request->OnProcessRequestComplete().BindRaw(this, &FMeshyApiClient::OnPollTaskResponse);
	Request->ProcessRequest();
}

void FMeshyApiClient::OnPollTaskResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bRequestInProgress)
	{
		return; // cancelled
	}

	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		// Non-fatal poll failure — will retry next tick
		return;
	}

	TSharedPtr<FJsonObject> JsonResp;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonResp) || !JsonResp.IsValid())
	{
		return;
	}

	FMeshyTaskResult Result = ParseTaskResult(JsonResp);

	if (Result.Status == TEXT("SUCCEEDED"))
	{
		bRequestInProgress = false;
		UE_LOG(LogTemp, Log, TEXT("UE_Gen Meshy: Task %s SUCCEEDED"), *Result.TaskId);
		OnTaskComplete.ExecuteIfBound(Result);
	}
	else if (Result.Status == TEXT("FAILED") || Result.Status == TEXT("CANCELED"))
	{
		bRequestInProgress = false;
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen Meshy: Task %s %s"), *Result.TaskId, *Result.Status);
		OnTaskComplete.ExecuteIfBound(Result);
	}
	else
	{
		// Still in progress
		OnTaskProgress.ExecuteIfBound(Result);
	}
}

FMeshyTaskResult FMeshyApiClient::ParseTaskResult(TSharedPtr<FJsonObject> JsonObj) const
{
	FMeshyTaskResult Result;
	JsonObj->TryGetStringField(TEXT("id"), Result.TaskId);
	JsonObj->TryGetStringField(TEXT("status"), Result.Status);
	double ProgressVal = 0.0;
	if (JsonObj->TryGetNumberField(TEXT("progress"), ProgressVal))
	{
		Result.Progress = FMath::RoundToInt32(ProgressVal);
	}
	JsonObj->TryGetStringField(TEXT("thumbnail_url"), Result.ThumbnailUrl);

	const TSharedPtr<FJsonObject>* ModelUrls;
	if (JsonObj->TryGetObjectField(TEXT("model_urls"), ModelUrls) && ModelUrls)
	{
		(*ModelUrls)->TryGetStringField(TEXT("glb"), Result.GLBUrl);
		(*ModelUrls)->TryGetStringField(TEXT("fbx"), Result.FBXUrl);
		(*ModelUrls)->TryGetStringField(TEXT("obj"), Result.OBJUrl);
	}

	const TSharedPtr<FJsonObject>* TextureUrls;
	if (JsonObj->TryGetObjectField(TEXT("texture_urls"), TextureUrls) && TextureUrls)
	{
		(*TextureUrls)->TryGetStringField(TEXT("base_color"), Result.BaseColorUrl);
		(*TextureUrls)->TryGetStringField(TEXT("normal"), Result.NormalUrl);
	}

	return Result;
}

void FMeshyApiClient::ListRecentTasks(int32 Limit)
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	if (Settings->MeshyApiKey.IsEmpty())
	{
		OnTaskListReceived.ExecuteIfBound(TArray<FMeshyTaskResult>());
		return;
	}

	// Meshy list endpoint: GET /image-to-3d?sortBy=-created_at&limit=N
	FString URL = FString::Printf(TEXT("%s/image-to-3d?sortBy=-created_at&limit=%d"),
		*MeshyBaseURL, Limit);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings->MeshyApiKey));
	Request->SetTimeout(15.0f);

	Request->OnProcessRequestComplete().BindRaw(this, &FMeshyApiClient::OnListTasksResponse);
	Request->ProcessRequest();
}

void FMeshyApiClient::OnListTasksResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	TArray<FMeshyTaskResult> Results;

	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen Meshy: Failed to list tasks (HTTP %d)"),
			Response.IsValid() ? Response->GetResponseCode() : 0);
		OnTaskListReceived.ExecuteIfBound(Results);
		return;
	}

	TSharedPtr<FJsonObject> JsonResp;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonResp) || !JsonResp.IsValid())
	{
		OnTaskListReceived.ExecuteIfBound(Results);
		return;
	}

	// Response could be {"data": [...]} or just [...]
	const TArray<TSharedPtr<FJsonValue>>* TasksArray = nullptr;
	if (!JsonResp->TryGetArrayField(TEXT("data"), TasksArray))
	{
		// Some API versions return the array at the top level — try parsing content as array
		TArray<TSharedPtr<FJsonValue>> TopArray;
		TSharedRef<TJsonReader<>> ArrReader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		if (FJsonSerializer::Deserialize(ArrReader, TopArray))
		{
			for (const auto& Val : TopArray)
			{
				TSharedPtr<FJsonObject> TaskObj = Val->AsObject();
				if (TaskObj.IsValid())
				{
					Results.Add(ParseTaskResult(TaskObj));
				}
			}
			UE_LOG(LogTemp, Log, TEXT("UE_Gen Meshy: Listed %d recent tasks"), Results.Num());
			OnTaskListReceived.ExecuteIfBound(Results);
			return;
		}
		OnTaskListReceived.ExecuteIfBound(Results);
		return;
	}

	for (const auto& Val : *TasksArray)
	{
		TSharedPtr<FJsonObject> TaskObj = Val->AsObject();
		if (TaskObj.IsValid())
		{
			Results.Add(ParseTaskResult(TaskObj));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("UE_Gen Meshy: Listed %d recent tasks"), Results.Num());
	OnTaskListReceived.ExecuteIfBound(Results);
}

void FMeshyApiClient::DownloadModel(const FString& GLBUrl, const FString& LocalSavePath)
{
	if (GLBUrl.IsEmpty())
	{
		OnModelDownloaded.ExecuteIfBound(TEXT(""));
		return;
	}

	CurrentDownloadPath = LocalSavePath;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(GLBUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(120.0f);

	Request->OnProcessRequestComplete().BindRaw(this, &FMeshyApiClient::OnDownloadResponse);
	Request->ProcessRequest();
}

void FMeshyApiClient::OnDownloadResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen Meshy: Failed to download model"));
		OnModelDownloaded.ExecuteIfBound(TEXT(""));
		return;
	}

	const TArray<uint8>& Content = Response->GetContent();
	if (FFileHelper::SaveArrayToFile(Content, *CurrentDownloadPath))
	{
		UE_LOG(LogTemp, Log, TEXT("UE_Gen Meshy: Model saved to %s (%d bytes)"), *CurrentDownloadPath, Content.Num());
		OnModelDownloaded.ExecuteIfBound(CurrentDownloadPath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UE_Gen Meshy: Failed to save model to %s"), *CurrentDownloadPath);
		OnModelDownloaded.ExecuteIfBound(TEXT(""));
	}
}
