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
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
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
