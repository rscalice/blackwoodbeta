// Blackwood Hollow - primary game module

using UnrealBuildTool;

public class BlackwoodHollowBeta : ModuleRules
{
	public BlackwoodHollowBeta(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"UMG",               // HUD widgets (UBH_HUDWidget)
			"AIModule",          // IGenericTeamAgentInterface / FGenericTeamId (combat team filtering)
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"Niagara",           // GameplayCue shatter VFX (UBH_GCN_PostureBroken)
			"EngineCameras",     // code-only camera shakes (UWaveOscillatorCameraShakePattern)
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
		});

		// Uncomment if/when online features are used.
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");
	}
}
