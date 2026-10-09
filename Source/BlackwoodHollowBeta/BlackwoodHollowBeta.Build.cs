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
			"AssetRegistry",     // editor-only profile asset creation (UBH_StanceMovementProfile::CreateProfileAsset)
			"UMG",               // HUD widgets (UBH_HUDWidget)
			"AIModule",          // IGenericTeamAgentInterface / FGenericTeamId (combat team filtering)
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"Niagara",           // GameplayCue shatter VFX (UBH_GCN_PostureBroken)
			"EngineCameras",     // code-only camera shakes (UWaveOscillatorCameraShakePattern)
			"NarrativeInventory", // Phase 8C: player inventory (UNarrativeInventoryComponent on the PlayerState)
			"NarrativeEquipment", // Phase 8C: UEquipmentComponent / UEquippableItem (UBH_WeaponItem)
			"RadialSelector",     // Phase 8C: stance radial menu (UBH_StanceRadialComponent)
			"ProceduralMeshComponent", // weapon swing trails (UBH_WeaponTrailComponent)
			"DeveloperSettings", // Phase 9: UBH_RPGSettings (Project Settings > Game > Blackwood Hollow RPG)
			"PhysicsCore",       // Phase 10A: EPhysicalSurface / UPhysicalMaterial::DetermineSurfaceType (footstep surfaces)
			"Narrative",         // Phase 11P: UNarrativePartyComponent / UNarrativeComponent (UBH_PartyComponent on ABH_GameState)
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"NavigationSystem",  // wave spawner: project spawn points onto the navmesh
			"CoreOnline",        // Phase 11C: FUniqueNetIdWrapper::ToString (per-player loot keys)
		});

		// Uncomment if/when online features are used.
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");
	}
}
