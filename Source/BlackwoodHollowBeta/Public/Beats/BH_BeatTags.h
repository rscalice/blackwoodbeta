// Blackwood Hollow - Island 1 beat tags (Phase 13B+)
// Target: Unreal Engine 5.8 (C++)
//
// One tag per beat of the Island 1 golden path, in route order. They are the BeatId column of DT_BH_Beats and the value the
// UBH_BeatTrackerComponent replicates as the party's current beat. Beat.Island1.Complete means every beat is done.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Beat_Island1_WreckCache);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Beat_Island1_Barricade);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Beat_Island1_Basin);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Beat_Island1_Obelisk);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Beat_Island1_ChokeExit);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Beat_Island1_Port);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Beat_Island1_WardenWatch);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Beat_Island1_Gatehouse);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Beat_Island1_Span);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Beat_Island1_Complete);