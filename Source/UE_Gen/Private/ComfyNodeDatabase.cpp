// Copyright UE_Gen. All Rights Reserved.

#include "ComfyNodeDatabase.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FComfyNodeDatabase& FComfyNodeDatabase::Get()
{
	static FComfyNodeDatabase Instance;
	return Instance;
}

/**
 * Strip emoji and other non-BMP Unicode characters that Slate cannot render.
 * Also trims any leading/trailing whitespace left behind after removal.
 */
static FString StripEmoji(const FString& Input)
{
	FString Result;
	Result.Reserve(Input.Len());

	const TCHAR* Ptr = *Input;
	while (*Ptr)
	{
		// UTF-16 surrogate pair (emoji / non-BMP) — skip both code units
		if (*Ptr >= 0xD800 && *Ptr <= 0xDBFF)
		{
			Ptr++; // high surrogate
			if (*Ptr >= 0xDC00 && *Ptr <= 0xDFFF)
			{
				Ptr++; // low surrogate
			}
			continue;
		}

		// Skip common emoji block characters in the BMP (misc symbols, dingbats,
		// variation selectors, zero-width joiners, etc.)
		TCHAR Ch = *Ptr;
		bool bSkip =
			(Ch >= 0x2600 && Ch <= 0x27BF) ||  // Misc Symbols + Dingbats
			(Ch >= 0x2B50 && Ch <= 0x2B55) ||  // Additional symbols
			(Ch >= 0xFE00 && Ch <= 0xFE0F) ||  // Variation Selectors
			(Ch == 0x200D) ||                   // Zero-Width Joiner
			(Ch == 0x20E3);                     // Combining Enclosing Keycap

		if (!bSkip)
		{
			Result.AppendChar(Ch);
		}
		Ptr++;
	}

	Result.TrimStartAndEndInline();
	return Result;
}

void FComfyNodeDatabase::ParseObjectInfo(TSharedPtr<FJsonObject> Root)
{
	if (!Root.IsValid()) return;

	NodeDefs.Empty();

	for (const auto& Pair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* NodeInfoPtr;
		if (!Pair.Value->TryGetObject(NodeInfoPtr) || !(*NodeInfoPtr).IsValid())
		{
			continue;
		}

		FComfyNodeDef Def = ParseSingleNode(Pair.Key, *NodeInfoPtr);
		NodeDefs.Add(Pair.Key, MoveTemp(Def));
	}

	UE_LOG(LogTemp, Log, TEXT("UE_Gen: ComfyNodeDatabase populated with %d node types"), NodeDefs.Num());

	OnDatabaseRefreshed.Broadcast();
}

FComfyNodeDef FComfyNodeDatabase::ParseSingleNode(const FString& ClassType, TSharedPtr<FJsonObject> NodeInfo) const
{
	FComfyNodeDef Def;
	Def.ClassType = ClassType;

	// Display name — strip emoji that Slate cannot render
	if (!NodeInfo->TryGetStringField(TEXT("display_name"), Def.DisplayName) || Def.DisplayName.IsEmpty())
	{
		Def.DisplayName = ClassType;
	}
	Def.DisplayName = StripEmoji(Def.DisplayName);
	if (Def.DisplayName.IsEmpty())
	{
		Def.DisplayName = ClassType;
	}

	// Category — also strip emoji from category paths
	NodeInfo->TryGetStringField(TEXT("category"), Def.Category);
	Def.Category = StripEmoji(Def.Category);

	// Description
	NodeInfo->TryGetStringField(TEXT("description"), Def.Description);

	// Output node flag
	NodeInfo->TryGetBoolField(TEXT("output_node"), Def.bIsOutputNode);

	// ---- Parse Inputs ----
	const TSharedPtr<FJsonObject>* InputObj;
	if (NodeInfo->TryGetObjectField(TEXT("input"), InputObj))
	{
		auto ParseInputGroup = [&Def](const TSharedPtr<FJsonObject>& GroupObj, bool bRequired)
		{
			if (!GroupObj.IsValid()) return;

			for (const auto& InputPair : GroupObj->Values)
			{
				FComfyInputDef InputDef;
				InputDef.Name = InputPair.Key;
				InputDef.bRequired = bRequired;

				const TArray<TSharedPtr<FJsonValue>>* InputArray;
				if (!InputPair.Value->TryGetArray(InputArray) || InputArray->Num() == 0)
				{
					continue;
				}

				// First element determines the type
				FString TypeStr;
				if ((*InputArray)[0]->TryGetString(TypeStr))
				{
					InputDef.Type = TypeStr;

					// If type is "COMBO" or a V3 dynamic combo, options are in element [1]
					// New format: ["COMBO", {"options": [...]}]
					// V3 format: ["COMFY_DYNAMICCOMBO_V3", {"options": [...], "default": "..."}]
					bool bIsComboLike = (TypeStr == TEXT("COMBO") || TypeStr.StartsWith(TEXT("COMFY_DYNAMICCOMBO")));
					if (bIsComboLike && InputArray->Num() >= 2)
					{
						// Normalize V3 dynamic combo to COMBO for consistent widget handling
						if (TypeStr != TEXT("COMBO"))
						{
							InputDef.Type = TEXT("COMBO");
						}

						const TSharedPtr<FJsonObject>* MetaObj;
						if ((*InputArray)[1]->TryGetObject(MetaObj))
						{
							const TArray<TSharedPtr<FJsonValue>>* Options;
							if ((*MetaObj)->TryGetArrayField(TEXT("options"), Options))
							{
								for (const auto& Opt : *Options)
								{
									FString OptStr;
									if (Opt->TryGetString(OptStr))
									{
										InputDef.ComboOptions.Add(OptStr);
									}
								}
							}

							// V3 types may carry a "default" string
							FString DefaultStr;
							if ((*MetaObj)->TryGetStringField(TEXT("default"), DefaultStr))
							{
								InputDef.DefaultString = DefaultStr;
							}
						}
					}
					else if (bIsComboLike)
					{
						// V3 dynamic combo with no meta object — treat as COMBO with no options
						InputDef.Type = TEXT("COMBO");
					}
				}
				else
				{
					// Old format: first element is an array of options (COMBO)
					const TArray<TSharedPtr<FJsonValue>>* OptionsArray;
					if ((*InputArray)[0]->TryGetArray(OptionsArray))
					{
						InputDef.Type = TEXT("COMBO");
						for (const auto& Opt : *OptionsArray)
						{
							FString OptStr;
							if (Opt->TryGetString(OptStr))
							{
								InputDef.ComboOptions.Add(OptStr);
							}
						}
					}
				}

				// Parse constraints from second element (if object)
				if (InputArray->Num() >= 2)
				{
					const TSharedPtr<FJsonObject>* ConstraintObj;
					if ((*InputArray)[1]->TryGetObject(ConstraintObj))
					{
						double Val;
						if ((*ConstraintObj)->TryGetNumberField(TEXT("default"), Val))
						{
							InputDef.DefaultNumber = Val;
						}
						if ((*ConstraintObj)->TryGetNumberField(TEXT("min"), Val))
						{
							InputDef.MinValue = Val;
						}
						if ((*ConstraintObj)->TryGetNumberField(TEXT("max"), Val))
						{
							InputDef.MaxValue = Val;
						}
						if ((*ConstraintObj)->TryGetNumberField(TEXT("step"), Val))
						{
							InputDef.Step = Val;
						}

						FString StrVal;
						if ((*ConstraintObj)->TryGetStringField(TEXT("default"), StrVal))
						{
							InputDef.DefaultString = StrVal;
						}
					}
				}

				Def.Inputs.Add(MoveTemp(InputDef));
			}
		};

		const TSharedPtr<FJsonObject>* RequiredObj;
		if ((*InputObj)->TryGetObjectField(TEXT("required"), RequiredObj))
		{
			ParseInputGroup(*RequiredObj, true);
		}

		const TSharedPtr<FJsonObject>* OptionalObj;
		if ((*InputObj)->TryGetObjectField(TEXT("optional"), OptionalObj))
		{
			ParseInputGroup(*OptionalObj, false);
		}
	}

	// ---- Parse Outputs ----
	const TArray<TSharedPtr<FJsonValue>>* OutputNames;
	if (NodeInfo->TryGetArrayField(TEXT("output"), OutputNames))
	{
		const TArray<TSharedPtr<FJsonValue>>* OutputNameDisplays;
		NodeInfo->TryGetArrayField(TEXT("output_name"), OutputNameDisplays);

		for (int32 i = 0; i < OutputNames->Num(); ++i)
		{
			FComfyOutputDef OutDef;
			(*OutputNames)[i]->TryGetString(OutDef.Type);

			if (OutputNameDisplays && i < OutputNameDisplays->Num())
			{
				(*OutputNameDisplays)[i]->TryGetString(OutDef.Name);
			}

			if (OutDef.Name.IsEmpty())
			{
				OutDef.Name = OutDef.Type;
			}

			Def.Outputs.Add(MoveTemp(OutDef));
		}
	}

	return Def;
}

const FComfyNodeDef* FComfyNodeDatabase::FindNode(const FString& ClassType) const
{
	return NodeDefs.Find(ClassType);
}

TArray<FString> FComfyNodeDatabase::GetCategories() const
{
	TSet<FString> CategorySet;
	for (const auto& Pair : NodeDefs)
	{
		if (!Pair.Value.Category.IsEmpty())
		{
			CategorySet.Add(Pair.Value.Category);
		}
	}

	TArray<FString> Result = CategorySet.Array();
	Result.Sort();
	return Result;
}

TArray<const FComfyNodeDef*> FComfyNodeDatabase::GetNodesInCategory(const FString& Category) const
{
	TArray<const FComfyNodeDef*> Result;
	for (const auto& Pair : NodeDefs)
	{
		if (Pair.Value.Category == Category)
		{
			Result.Add(&Pair.Value);
		}
	}

	Result.Sort([](const FComfyNodeDef& A, const FComfyNodeDef& B)
	{
		return A.DisplayName < B.DisplayName;
	});

	return Result;
}

TArray<const FComfyNodeDef*> FComfyNodeDatabase::SearchNodes(const FString& Query) const
{
	TArray<const FComfyNodeDef*> Result;
	FString LowerQuery = Query.ToLower();

	for (const auto& Pair : NodeDefs)
	{
		if (Pair.Value.DisplayName.ToLower().Contains(LowerQuery) ||
			Pair.Value.ClassType.ToLower().Contains(LowerQuery) ||
			Pair.Value.Category.ToLower().Contains(LowerQuery))
		{
			Result.Add(&Pair.Value);
		}
	}

	// Sort by relevance: exact class_type match first, then display name match
	Result.Sort([&LowerQuery](const FComfyNodeDef& A, const FComfyNodeDef& B)
	{
		bool AExact = A.ClassType.ToLower() == LowerQuery;
		bool BExact = B.ClassType.ToLower() == LowerQuery;
		if (AExact != BExact) return AExact;
		return A.DisplayName < B.DisplayName;
	});

	return Result;
}
