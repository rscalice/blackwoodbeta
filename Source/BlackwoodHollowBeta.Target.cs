// Blackwood Hollow - Game target

using UnrealBuildTool;
using System.Collections.Generic;

public class BlackwoodHollowBetaTarget : TargetRules
{
	public BlackwoodHollowBetaTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "BlackwoodHollowBeta" });
	}
}
