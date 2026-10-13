// Blackwood Hollow - editor-only module (Blueprint authoring helpers). Never loaded in packaged games.

using UnrealBuildTool;

public class BlackwoodHollowBetaEditor : ModuleRules
{
	public BlackwoodHollowBetaEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"BlueprintGraph",
			"Kismet",
		});
	}
}