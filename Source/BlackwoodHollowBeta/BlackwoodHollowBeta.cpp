// Blackwood Hollow - primary game module implementation

#include "BlackwoodHollowBeta.h"
#include "AbilitySystem/BH_GameplayTags.h"

void FBlackwoodHollowBetaModule::StartupModule()
{
	FBH_GameplayTags::InitializeNativeTags();
}

IMPLEMENT_PRIMARY_GAME_MODULE(FBlackwoodHollowBetaModule, BlackwoodHollowBeta, "BlackwoodHollowBeta");
