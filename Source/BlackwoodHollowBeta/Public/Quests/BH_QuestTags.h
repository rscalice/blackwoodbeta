
// Blackwood Hollow - quest native gameplay tags (Phase 14A-1)
// Target: Unreal Engine 5.8 (C++)
//
// Quest.Target is the root of the quest target tags. A placed ABH_QuestTarget carries one tag UNDER it ("Quest.Target.Island1.WreckCache"), and a
// row of DT_BH_QuestTargets (FBH_QuestTargetRow::TargetTag) names the same tag, which is how a quest task finds where to point the compass.
// Only the root is native (it makes the "Categories" filter on the tag pickers work); the children are authored per level in
// Config/Tags/*.ini by the 14A-2 asset step, exactly like the Beat.Island1.* children were.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Quest_Target);
