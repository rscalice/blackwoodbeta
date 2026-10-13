// Blackwood Hollow - editor-only Blueprint comment helpers

#include "BH_BlueprintCommentLibrary.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "FileHelpers.h"
#include "Editor.h"

namespace BH_CommentLib
{
	/** Every graph the Blueprint owns (event graphs, functions, macros, delegate signatures). Widget BPs use the same lists. */
	static void GatherGraphs(const UBlueprint* BP, TArray<UEdGraph*>& Out)
	{
		Out.Append(BP->UbergraphPages);
		Out.Append(BP->FunctionGraphs);
		Out.Append(BP->MacroGraphs);
		Out.Append(BP->DelegateSignatureGraphs);
	}

	static UEdGraph* FindGraph(const UBlueprint* BP, FName GraphName)
	{
		if (!BP) { return nullptr; }
		TArray<UEdGraph*> Graphs;
		GatherGraphs(BP, Graphs);
		for (UEdGraph* G : Graphs)
		{
			if (G && G->GetFName() == GraphName) { return G; }
		}
		return nullptr;
	}

	static UEdGraphNode* FindNode(const UEdGraph* Graph, FName NodeName)
	{
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N && N->GetFName() == NodeName) { return N; }
		}
		return nullptr;
	}

	/** Slate-measured size when the node has been drawn; otherwise a rough estimate from the pin count. */
	static FVector2D NodeSize(const UEdGraphNode* N)
	{
		const float W = N->NodeWidth > 0 ? N->NodeWidth : 260.f;
		const float H = N->NodeHeight > 0 ? N->NodeHeight : 60.f + 16.f * FMath::Max(1, N->Pins.Num() / 2);
		return FVector2D(W, H);
	}
}

bool UBH_BlueprintCommentLibrary::SetNodeComment(UBlueprint* Blueprint, FName GraphName, FName NodeName, const FString& Comment, bool bPinnedBubble)
{
	UEdGraph* Graph = BH_CommentLib::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BH_Comment] Graph '%s' not found"), *GraphName.ToString());
		return false;
	}
	UEdGraphNode* Node = BH_CommentLib::FindNode(Graph, NodeName);
	if (!Node)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BH_Comment] Node '%s' not found in '%s'"), *NodeName.ToString(), *GraphName.ToString());
		return false;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Set Node Comment")));
	Node->Modify();
	Node->NodeComment = Comment;
	Node->bCommentBubblePinned = bPinnedBubble;
	Node->bCommentBubbleVisible = bPinnedBubble;
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

FString UBH_BlueprintCommentLibrary::AddCommentBox(UBlueprint* Blueprint, FName GraphName, const TArray<FName>& NodeNames, const FString& Text, FLinearColor Color, int32 Padding)
{
	UEdGraph* Graph = BH_CommentLib::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BH_Comment] Graph '%s' not found"), *GraphName.ToString());
		return FString();
	}

	FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
	int32 Found = 0;
	for (const FName& Name : NodeNames)
	{
		const UEdGraphNode* N = BH_CommentLib::FindNode(Graph, Name);
		if (!N)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BH_Comment] Node '%s' not found in '%s' (skipped)"), *Name.ToString(), *GraphName.ToString());
			continue;
		}
		const FVector2D Size = BH_CommentLib::NodeSize(N);
		Min.X = FMath::Min<double>(Min.X, N->NodePosX);
		Min.Y = FMath::Min<double>(Min.Y, N->NodePosY);
		Max.X = FMath::Max<double>(Max.X, N->NodePosX + Size.X);
		Max.Y = FMath::Max<double>(Max.Y, N->NodePosY + Size.Y);
		++Found;
	}
	if (Found == 0) { return FString(); }

	const FScopedTransaction Transaction(FText::FromString(TEXT("Add Comment Box")));
	Graph->Modify();

	UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(Graph, UEdGraphNode_Comment::StaticClass(), NAME_None, RF_Transactional);
	Graph->AddNode(Comment, /*bFromUI*/ false, /*bSelectNewNode*/ false);
	Comment->CreateNewGuid();
	Comment->PostPlacedNewNode();
	Comment->AllocateDefaultPins();

	const double TitleSpace = 36.0; // room above the nodes for the comment title
	Comment->NodePosX = FMath::RoundToInt(Min.X - Padding);
	Comment->NodePosY = FMath::RoundToInt(Min.Y - Padding - TitleSpace);
	Comment->NodeWidth = FMath::RoundToInt((Max.X - Min.X) + 2 * Padding);
	Comment->NodeHeight = FMath::RoundToInt((Max.Y - Min.Y) + 2 * Padding + TitleSpace);
	Comment->NodeComment = Text;
	Comment->CommentColor = Color;

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return Comment->GetName();
}

TArray<FString> UBH_BlueprintCommentLibrary::ListGraphNodes(UBlueprint* Blueprint, FName GraphName)
{
	TArray<FString> Lines;
	if (!Blueprint) { return Lines; }

	TArray<UEdGraph*> Graphs;
	if (GraphName.IsNone())
	{
		BH_CommentLib::GatherGraphs(Blueprint, Graphs);
	}
	else if (UEdGraph* G = BH_CommentLib::FindGraph(Blueprint, GraphName))
	{
		Graphs.Add(G);
	}

	for (const UEdGraph* G : Graphs)
	{
		if (!G) { continue; }
		for (const UEdGraphNode* N : G->Nodes)
		{
			if (!N) { continue; }
			FString Title = N->GetNodeTitle(ENodeTitleType::ListView).ToString();
			Title.ReplaceInline(TEXT("\r"), TEXT(" "));
			Title.ReplaceInline(TEXT("\n"), TEXT(" "));
			const FString Prefix = GraphName.IsNone() ? FString::Printf(TEXT("[%s] "), *G->GetName()) : FString();
			Lines.Add(FString::Printf(TEXT("%s%s | %s | %d,%d"), *Prefix, *N->GetName(), *Title, N->NodePosX, N->NodePosY));
		}
	}
	return Lines;
}

bool UBH_BlueprintCommentLibrary::CompileBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint) { return false; }
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error;
}

bool UBH_BlueprintCommentLibrary::SaveBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint) { return false; }
	TArray<UPackage*> Packages;
	Packages.Add(Blueprint->GetOutermost());
	return UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty*/ false);
}
