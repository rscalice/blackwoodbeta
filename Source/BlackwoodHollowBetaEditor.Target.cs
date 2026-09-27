// Blackwood Hollow - Editor target

using UnrealBuildTool;
using System.Collections.Generic;

public class BlackwoodHollowBetaEditorTarget : TargetRules
{
	public BlackwoodHollowBetaEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "BlackwoodHollowBeta" });
	}
}
